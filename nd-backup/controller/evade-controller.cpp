/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/evade-controller.h"

#include "corelib/math/segment-util.h"

#include "ndlib/anim/anim-action.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-util.h"
#include "ndlib/process/debug-selection.h"

#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/ndphys/object-avoidance-tracker.h"
#include "gamelib/tasks/nd-task-manager.h"

#include "game/ai/agent/npc-manager.h"
#include "game/ai/agent/npc.h"
#include "game/ai/base/ai-game-debug.h"
#include "game/ai/behavior/behavior-follow.h"
#include "game/ai/behavior/behavior-move-to.h"
#include "game/ai/behavior/behavior-open.h"
#include "game/ai/characters/buddy.h"
#include "game/ai/characters/dog.h"
#include "game/ai/characters/human.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/skill/skill.h"
#include "game/player/player-snapshot.h"
#include "game/weapon/projectile-throwable.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AiEvadeController : public IAiEvadeController
{
public:
	AiEvadeController();

	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override;
	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;
	virtual bool IsBusy() const override;
	virtual bool ShouldInterruptSkills() const override;
	virtual bool ShouldInterruptNavigation() const override;
	//virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const override {}
	virtual bool IsEvading() const override;
	virtual void EnableEvade(bool enable) override { m_enableEvade = enable; }
	virtual bool IsEvadeEnabled() const override { return m_enableEvade; }
	virtual bool HasEvadeAnims() const override { return m_hasEvadeAnims; }

	virtual void RequestEvadeSkillOverride() override { m_evadeExternallyEnabled = true; }

private:
	bool IsValidStateToEvade(const NavCharacter* pNavChar) const;
	bool TryEvade(NavCharacter* pNavChar);
	bool ShouldEvadePlayer(const NavCharacter* pNavChar, const PlayerSnapshot* const pPlayer) const;

	static CONST_EXPR float kPlayerEvadePhaseDuration = 0.30f;
	static CONST_EXPR float kNpcEvadePhaseDuration = 0.60f;
	static CONST_EXPR float kObjectEvadePhaseDuration = 0.50f;

	AnimAction m_evadeAction;
	F32 m_evadePhase;
	U32 m_evadeThreatsMask;
	bool m_enableEvade;
	bool m_isValidState;
	bool m_hasEvadeAnims;
	bool m_dog;
	bool m_evadeExternallyEnabled;

	enum EvadeDirs
	{
		kForward,
		kBackward,
		kLeft,
		kRight,

		kDirCount
	};

	enum EvadeType
	{
		kPlayer,
		kNpc,
		kObject,

		kTypeCount
	};

	CachedAnimLookup m_evadeAnims[kDirCount];
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiEvadeController* CreateAiEvadeController()
{
	return NDI_NEW AiEvadeController();
}

/// --------------------------------------------------------------------------------------------------------------- ///
AiEvadeController::AiEvadeController()
	: m_evadePhase(0.0f), m_enableEvade(true), m_evadeThreatsMask(0U), m_dog(false), m_isValidState(false)
{
	m_evadeAnims[kForward].SetSourceId(SID("evade-forward"));
	m_evadeAnims[kBackward].SetSourceId(SID("evade-backward"));
	m_evadeAnims[kLeft].SetSourceId(SID("evade-left"));
	m_evadeAnims[kRight].SetSourceId(SID("evade-right"));
	m_hasEvadeAnims = false;

	m_evadeExternallyEnabled = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiEvadeController::Init(NavCharacter* pNavChar, const NavControl* pNavControl)
{
	AI_ASSERT(pNavChar);
	AI_ASSERT(pNavControl);

	IAiEvadeController::Init(pNavChar, pNavControl);

	m_dog = pNavChar->IsKindOf(g_type_Dog);
	m_isValidState = false;
	m_evadeExternallyEnabled = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiEvadeController::RequestAnimations()
{
	NavCharacter* pNavChar = GetCharacter();
	if (!pNavChar)
		return;
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	if (!pAnimControl)
		return;

	m_hasEvadeAnims = true;

	for (int i = 0; i < kDirCount; ++i)
	{
		m_evadeAnims[i] = pAnimControl->LookupAnimCached(m_evadeAnims[i]);

		if (nullptr == m_evadeAnims[i].GetAnim().ToArtItem())
		{
			m_hasEvadeAnims = false;
		}
	}

	m_isValidState = IsValidStateToEvade(pNavChar);

	m_evadeExternallyEnabled = false;

	if (m_isValidState && !FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_buddy.m_disableEvades))
	{
		TryEvade(pNavChar);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiEvadeController::UpdateStatus()
{
	AnimControl* pAnimControl = GetCharacter()->GetAnimControl();
	m_evadeAction.Update(pAnimControl);
	m_evadePhase = m_evadeAction.GetAnimPhase(pAnimControl);

	if (m_evadePhase >= kPlayerEvadePhaseDuration)
		ClearBit(m_evadeThreatsMask, kPlayer);

	if (m_evadePhase >= kNpcEvadePhaseDuration)
		ClearBit(m_evadeThreatsMask, kNpc);

	if (m_evadePhase >= kObjectEvadePhaseDuration)
		ClearBit(m_evadeThreatsMask, kObject);

	if (!m_evadeAction.IsValid() || m_evadeAction.IsDone())
	{
		m_evadeThreatsMask = 0U;
	}

	if (m_evadeAction.IsValid() && m_evadeAction.IsDone())
	{
		m_evadeAction.Reset();
		Npc* const pMutableNpc = Npc::FromProcess(GetCharacter());
		if (pMutableNpc)
		{
			if (pMutableNpc->GetBuddyLookAtLogic())
			{
				pMutableNpc->ClearLookAimMode(kLookAimPriorityBehaviorHigh, FILE_LINE_FUNC);
			}
		}
	}

	if (m_evadeAction.IsValid() && !m_evadeAction.IsDone())
	{
		Npc* const pMutableNpc = Npc::FromProcess(GetCharacter());
		if (pMutableNpc)
		{
			if (pMutableNpc->GetBuddyLookAtLogic())
			{
				pMutableNpc->SetLookAimMode(kLookAimPriorityBehaviorHigh, SID("LookAimBuddy"), FILE_LINE_FUNC);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiEvadeController::IsBusy() const
{
	return IsEvading() && m_evadePhase < 0.33333333f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiEvadeController::ShouldInterruptSkills() const
{
	if (!IsBusy())
	{
		return false;
	}

	const Npc* const pNpc = Npc::FromProcess(GetCharacter());

	if (pNpc && pNpc->IsInScriptedControlState())
	{
		return false;
	}

	if (pNpc->GetDesiredSkillNum() == DC::kAiArchetypeSkillBuddyCombat ||
		pNpc->GetDesiredSkillNum() == DC::kAiArchetypeSkillBuddySnowball ||
		pNpc->GetDesiredSkillNum() == DC::kAiArchetypeSkillSneak ||
		pNpc->GetDesiredSkillNum() == DC::kAiArchetypeSkillFollow ||
		pNpc->GetDesiredSkillNum() == DC::kAiArchetypeSkillExplore)
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiEvadeController::ShouldInterruptNavigation() const
{
	return IsBusy();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiEvadeController::IsEvading() const
{
	return m_evadeAction.IsValid() && !m_evadeAction.IsDone();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool RejectEvade(const NavCharacter* const pNavChar, const char* s)
{
	STRIP_IN_FINAL_BUILD_VALUE(false);

	if (g_aiGameOptions.m_buddy.m_displayEvade && DebugSelection::Get().IsProcessOrNoneSelected(pNavChar))
	{
		g_prim.Draw(DebugString(pNavChar->GetTranslation() + Vector(0.0f, 1.5f, 0.0f), s, 1, kColorRed, 1.0f), kPrimDuration1FramePauseable);
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void RejectEvade(const NavCharacter* pNavChar, const Point startWs, const Point endWs, const char* s)
{
	STRIP_IN_FINAL_BUILD;

	if (g_aiGameOptions.m_buddy.m_displayEvade && DebugSelection::Get().IsProcessOrNoneSelected(pNavChar))
	{
		g_prim.Draw(DebugString(Lerp(startWs, endWs, 0.5f), s, 1, kColorRed, 1.0f), kPrimDuration1FramePauseable);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiEvadeController::ShouldEvadePlayer(const NavCharacter* pNavChar, const PlayerSnapshot* const pPlayer) const
{
	if (!pPlayer)
	{
		return RejectEvade(pNavChar, "kNoPlayer");
	}

	if (pPlayer->GetStateId() != SID("Journal"))
	{
		const bool playerNotMoving = LengthSqr(pPlayer->GetVelocityWs()) < Sqr(0.15f);
		const bool noStickRequested = pPlayer->GetStickSpeed(NdPlayerJoypad::kStickMove) == 0.0f;

		const float distToPlayer = DistXz(pNavChar->GetTranslation(), pPlayer->GetTranslation());
		const bool playerIsNotSuperClose = distToPlayer > 0.4f; // super close = severely interpenetrating geo

		if (playerNotMoving && noStickRequested && playerIsNotSuperClose)
		{
			return RejectEvade(pNavChar, "kPlayerNotTryingToMove");
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiEvadeController::IsValidStateToEvade(const NavCharacter* pNavChar) const
{
	// determine whether we are even capable of evasion
	AI_ASSERT(pNavChar);

	if (!m_enableEvade)
		return RejectEvade(pNavChar, "kEvadeDisabled");

	if (!HasEvadeAnims())
		return RejectEvade(pNavChar, "kEvadeAnimsUnavailable");

	if (pNavChar->IsDead())
		return RejectEvade(pNavChar, "kDead");

	{
		const Demeanor curDemeanor = pNavChar->GetCurrentDemeanor();
		const Demeanor reqDemeanor = pNavChar->GetRequestedDemeanor();
		if (curDemeanor == kDemeanorCrawl || reqDemeanor == kDemeanorCrawl)
			return RejectEvade(pNavChar, "kProne");

		if (curDemeanor == kDemeanorCrouch || reqDemeanor == kDemeanorCrouch)
			return RejectEvade(pNavChar, "kCrouch");
	}

	const Npc* const pNpc = Npc::FromProcess(pNavChar);

	const PlayerSnapshot* const pPlayer = GetPlayerSnapshot();
	if (!pPlayer)
		return RejectEvade(pNavChar, "kNoPlayer");

	if (g_netInfo.IsNetActive())
		return RejectEvade(pNavChar, "kNet");

	if (!IsEvading() && !pNavChar->CanProcessNavCommands())
	{
		bool allowAnyway = false;

		if (pNpc)
		{
			if (const Skill* const pSkill = pNpc->GetActiveSkill())
			{
				if (pSkill->GetSkillNum() == DC::kAiArchetypeSkillExplore)
				{
					if (pSkill->IsState(SID("ChooseNextAction")))
					{
						if (const IAiPerformanceController* const pPerfCon = pNpc->GetAnimationControllers()->GetPerformanceController())
						{
							if (pPerfCon->IsPerformancePlaying())
							{
								allowAnyway = true;
							}
						}
					}
				}
			}
		}

		if (!allowAnyway)
		{
			return RejectEvade(pNavChar, "kCantProcessNavCmds");
		}
	}

	{
		const ActionPack* const pAp = pNavChar->GetEnteredActionPack();
		if (pAp && pAp->GetType() == ActionPack::kCinematicActionPack)
			return RejectEvade(pNavChar, "kCap");
	}

	if (pNavChar->IsInScriptedAnimationState())
		return RejectEvade(pNavChar, "kScriptedAnimState");

	if (pNavChar->IsSwimming())
		return RejectEvade(pNavChar, "kSwimming");

	if (pNavChar->ShouldForcePlayerPush())
		return RejectEvade(pNavChar, "kPlayerPush");

	if (const IAiRideHorseController* const pRideHorseController = pNavChar->GetAnimationControllers()->GetRideHorseController())
	{
		if (pRideHorseController->IsRidingHorse())
		{
			return RejectEvade(pNavChar, "kRidingHorse");
		}
	}

	if (pNpc && !pNpc->GetConfiguration().m_canEvade)
	{
		return RejectEvade(pNavChar, "kConfigDisallowsEvade");
	}

	if (m_evadeExternallyEnabled)
	{
		return true;
	}

	if (pNpc && pNpc->IsInScriptedControlState())
	{
		return RejectEvade(pNavChar, "kScriptedControlState");
	}

	if (!IsEvading())
	{
		switch (GetTensionMode())
		{
		case DC::kTensionModeSearch:
			return RejectEvade(pNavChar, "kNotInAllowedTension");
		case DC::kTensionModeCombat:
		{
			// allowed if NPC is in combat and offscreen
			if (pNpc)
			{
				const StringId64 skillId = pNpc->GetActiveSkillNameId();
				if (skillId != SID("BuddyCombatSkill"))
				{
					return RejectEvade(pNavChar, "kNotInBuddyCombatSkill");
				}

				const AiBehavior* pBehaviorMoveTo = pNpc->GetBehaviorInTreeAs<BehaviorMoveTo>();
				const AiBehavior* pBehaviorOpen = pNpc->GetBehaviorInTreeAs<BehaviorOpen>();
				if (!pBehaviorMoveTo && !pBehaviorOpen)
				{
					return RejectEvade(pNavChar, "kNotInAllowedBehavior");
				}

				if (!pNpc->IsOffscreen())
				{
					return RejectEvade(pNavChar, "kOnscreen");
				}
			}
		}
		break;
		case DC::kTensionModeUnaware:
		case DC::kTensionModeNormal:
		case DC::kTensionModeSlow:
			{
				// allowed if NPC is in follow/explore/idle
				if (!pNavChar->IsBuddyNpc())
				{
					bool reject = true;
					if (pNpc && m_dog)
					{
						if (const DC::AiArchetype* const pArchetype = pNpc->GetArchetype().GetInfo())
						{
							if (pArchetype->m_dogCanEvade)
							{
								reject = false;
							}
						}
					}
					if (reject)
						return RejectEvade(pNavChar, "kNotABuddy");
				}
				if (pNpc)
				{
					if (const BehaviorFollow* const pBehaviorFollow = pNpc->GetBehaviorInTreeAs<BehaviorFollow>())
					{
						if (!pBehaviorFollow->GetLeaderHandle().IsKindOf(g_type_PlayerBase))
						{
							return RejectEvade(pNavChar, "kFollowingNpc");
						}
					}

					const StringId64 skillId = pNpc->GetActiveSkillNameId();
					if (skillId != SID("FollowSkill") && skillId != SID("ExploreSkill")
						&& skillId != SID("BuddySnowballSkill") && skillId != SID("IdleSkill")
						&& skillId != SID("HeelFollowSkill"))
					{
						if (skillId == SID("LeadSkill"))
						{
							return RejectEvade(pNavChar, "kLeadSkill");
						}

						const AiBehavior* pBehavior = pNpc->GetBehavior();
						if (!pBehavior)
						{
							return RejectEvade(pNavChar, "kNotInAllowedBehavior");
						}

						if (pBehavior->GetTypeId() != SID("BehaviorFollow")
							&& pBehavior->GetTypeId() != SID("BehaviorAmbientWait")
							&& pBehavior->GetTypeId() != SID("BehaviorMoveTo"))
						{
							return RejectEvade(pNavChar, "kNotInAllowedBehavior");
						}
					}
				}
			}
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Nav::StaticBlockageMask GetStaticBlockageMask(const NavCharacter* const pNavChar)
{
	return pNavChar->IsKindOf(g_type_Buddy) ? Nav::kStaticBlockageMaskHuman | Nav::kStaticBlockageMaskBuddy
											: Nav::kStaticBlockageMaskHuman;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiEvadeController::TryEvade(NavCharacter* pNavChar)
{
	StringId64 taskNameId = INVALID_STRING_ID_64;
	if (const NdTaskManager* pTaskMgr = g_hTaskManager.ToProcess())
	{
		if (pTaskMgr->m_gameTaskSubnode)
			taskNameId = pTaskMgr->m_gameTaskSubnode->m_fullName;
	}
	const bool aliceFetch = taskNameId == SID("amputation-amp-yara-walk-alice-fetch") && pNavChar->GetUserId() == SID("yara");

	const PlayerSnapshot* const pPlayer = GetPlayerSnapshot();
	if (!pPlayer)
		return RejectEvade(pNavChar, "kNoPlayer");

	// compute some info about me
	const Point meWs = pNavChar->GetTranslation();
	const Vector meFwdWs = SafeNormalize(VectorXz(GetLocalZ(pNavChar->GetRotation())), kUnitZAxis);

	const Vector meVelWs = VectorXz(pNavChar->GetVelocityWs());
	const float meSpeed = Length(meVelWs);
	const bool meIsMoving = pNavChar->IsMoving() && pNavChar->IsCommandInProgress();
	const Vector meDirWs = meVelWs / meSpeed;
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
	const NavLocation& meNavLoc = pNavChar->GetNavLocation();
	if (!meNavLoc.IsValid())
		return RejectEvade(pNavChar, "kInvalidMeNavLoc");

	CONST_EXPR float kFixedCharProbeRadius = 0.25f;
	//const float charProbeRadius = GetNavControl()->GetMovingNavAdjustRadius();
	float charProbeRadius = kFixedCharProbeRadius;
	const NavMesh* pMeMesh = meNavLoc.ToNavMesh();
	AI_ASSERT(pMeMesh);
	const NavPoly* pMePoly = meNavLoc.ToNavPoly();
	AI_ASSERT(pMePoly);

	// depen my start pos
	Point meDepenWs = kInvalidPoint;
	{
		NavMesh::FindPointParams params;
		params.m_point = pMeMesh->WorldToParent(meWs);
		params.m_pStartPoly = pMePoly;
		params.m_crossLinks = true;
		params.m_minNpcStature = NavMesh::NpcStature::kStand;
		params.m_depenRadius = charProbeRadius + 0.02f;
		params.m_searchRadius = params.m_depenRadius + 0.1f;
		params.m_obeyedStaticBlockers = GetStaticBlockageMask(pNavChar);
		pMeMesh->FindNearestPointPs(&params);
		if (!params.m_pPoly)
		{
			// nope
			return RejectEvade(pNavChar, "kDepenFail");
		}

		// successfully adjusted by depenetration probe, potentially onto new mesh, poly, and point
		// update all 3 quantities
		pMePoly = params.m_pPoly;
		pMeMesh = pMePoly->GetNavMesh();

		// preserve old Y though
		meDepenWs = PointFromXzAndY(pMeMesh->ParentToWorld(params.m_nearestPoint), meWs);
	}
	NAV_ASSERT(IsReasonable(meDepenWs));

	// adjust test point to compensate for my velocity
	const NavMesh* pMeTestMesh = pMeMesh;
	const NavPoly* pMeTestPoly = pMePoly;
	Point meTestWs = meWs;
	if (meSpeed > 0.01f)
	{
		const Vector velOfsWs = 0.26f * Min(1.8f, meSpeed) * meDirWs;
		NavMesh::ProbeParams params;
		params.m_start = pMeMesh->WorldToParent(meDepenWs);
		params.m_pStartPoly = pMePoly;
		params.m_move = pMeMesh->WorldToParent(velOfsWs);
		params.m_probeRadius = charProbeRadius;
		params.m_obeyedStaticBlockers = GetStaticBlockageMask(pNavChar);
		const NavMesh::ProbeResult res = pMeMesh->ProbePs(&params);
		if (res == NavMesh::ProbeResult::kReachedGoal || res == NavMesh::ProbeResult::kHitEdge)
		{
			pMeTestPoly = params.m_pReachedPoly;
			pMeTestMesh = pMeTestPoly->GetNavMesh();

			// preserve old Y though
			meTestWs = PointFromXzAndY(pMeTestMesh->ParentToWorld(params.m_endPoint), meWs);
		}
	}

	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_buddy.m_displayEvade))
	{
		const Vector yOfs(0.0f, 0.09f, 0.0f);
		g_prim.Draw(DebugArrow(meWs + yOfs, meTestWs + yOfs, kColorBlack), kPrimDuration1FramePauseable);
	}

	// prepare and compute some preliminary info about a selection of possible evades
	// generate some possible evade dirs
	CONST_EXPR int kNumTests = 36;
	CONST_EXPR float kTestSpacing = PI_TIMES_2 / kNumTests;
	const Vector meLocalZWs = VectorXz(GetLocalZ(pNavChar->GetRotation()));
	const SkeletonId skelId = pNavChar->GetSkeletonId();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const Locator meLocWs = pNavChar->GetLocator();

	BitArray<kNumTests> evadeValid;
	evadeValid.ClearAllBits();

	const ArtItemAnim* apEvadeAnims[kNumTests];
	EvadeDirs evadeDirs[kNumTests];
	Locator evadeApRefWs[kNumTests];
	Vector evadeAnimDirWs[kNumTests];
	Point evadeFinalPosWs[kNumTests];
	for (int iTest = 0; iTest < kNumTests; ++iTest)
	{
		const float theta = iTest * kTestSpacing;
		const Vector testDirWs = RotateVectorAbout(meLocalZWs, kUnitYAxis, theta);

		const float dot = Dot(meLocalZWs, testDirWs);
		const float crossY = CrossY(meLocalZWs, testDirWs);
		Vector evadeRefVecWs;

		if (dot >= 0.70710678118f)
		{
			evadeDirs[iTest] = kForward;
			evadeRefVecWs = meLocalZWs;
		}
		else if (dot <= -0.70710678118f)
		{
			evadeDirs[iTest] = kBackward;
			evadeRefVecWs = -meLocalZWs;
		}
		else if (crossY > 0.0f)
		{
			evadeDirs[iTest] = kLeft;
			evadeRefVecWs = RotateY90(meLocalZWs);
		}
		else
		{
			evadeDirs[iTest] = kRight;
			evadeRefVecWs = RotateYMinus90(meLocalZWs);
		}

		const ArtItemAnim* pEvadeAnim = m_evadeAnims[evadeDirs[iTest]].GetAnim().ToArtItem();
		AI_ASSERT(pEvadeAnim);

		apEvadeAnims[iTest] = pEvadeAnim;

		const Locator apRefWs = Locator(meLocWs.Pos(), meLocWs.Rot() * QuatFromVectors(testDirWs, evadeRefVecWs));
		Locator alignWs(kIdentity);
		const bool foundAlign = FindAlignFromApReference(skelId, pEvadeAnim, 1.0f, apRefWs, SID("apReference"), &alignWs);
		AI_ASSERT(foundAlign);
		evadeApRefWs[iTest] = apRefWs;

		// nav probe
		NavMesh::ProbeParams params;
		params.m_start = pMeMesh->WorldToParent(meDepenWs);
		params.m_pStartPoly = pMePoly;
		params.m_move = pMeMesh->WorldToParent(alignWs.Pos()) - params.m_start;
		params.m_probeRadius = charProbeRadius;
		params.m_crossLinks = true;
		params.m_minNpcStature = NavMesh::NpcStature::kStand;
		params.m_obeyedStaticBlockers = GetStaticBlockageMask(pNavChar);
		const NavMesh::ProbeResult res = pMeMesh->ProbePs(&params);
		if (res != NavMesh::ProbeResult::kReachedGoal && res != NavMesh::ProbeResult::kHitEdge)
			continue;

		const Point afterEvadeWs = PointFromXzAndY(params.m_pReachedPoly->GetNavMesh()->ParentToWorld(params.m_endPoint), meDepenWs);
		evadeFinalPosWs[iTest] = afterEvadeWs;
		const Vector animVecWs = afterEvadeWs - meWs;
		const float animVecLen = Length(animVecWs);

		if (animVecLen < 0.01f)
			continue;

		const Vector animDirWs = Normalize(animVecWs);
		evadeAnimDirWs[iTest] = animDirWs;

		const float remAnimDist = Dist(alignWs.Pos(), meWs) - animVecLen;
		if (remAnimDist > 0.85f)
			continue;

		evadeValid.SetBit(iTest);
	}

	if (evadeValid.AreAllBitsClear())
		return RejectEvade(pNavChar, "kNoViableEvades");

	// compute some info about direct player segment
	const Point playerPosWs = pPlayer->GetTranslation();
	const bool playerIsProne = pPlayer->IsProne();
	const bool playerIsCrouched = !playerIsProne && pPlayer->IsCrouched();
	const Vector playerVelHistory0Ws = VectorXz(pPlayer->GetVelHistory0Ws());
	const Vector playerVelHistory1Ws = VectorXz(pPlayer->GetVelHistory1Ws());
	const Vector playerVelWs = VectorXz(pPlayer->GetVelocityWs());
	const Point complexPredWs = pPlayer->GetBuddyEvadePredictedWs();
	const bool haveComplexPred = !AllComponentsEqual(complexPredWs, kInvalidPoint);
	bool haveMeleePred = false;
	Point playerProbeStartWs = kInvalidPoint;
	Point playerProbeEndWs = kInvalidPoint;
	Point playerProbeResWs = kInvalidPoint;
	if (haveComplexPred)
	{
		AI_ASSERT(IsReasonable(playerPosWs));
		AI_ASSERT(IsReasonable(complexPredWs));

		playerProbeStartWs = playerPosWs;
		playerProbeEndWs = complexPredWs;
		playerProbeResWs = complexPredWs;
	}
	else
	{
		// no complex prediction available. do navigational prediction instead

		Vector playerPredVelWs = playerVelWs;
		const float playerVelLen = Length(playerVelWs);
		if (!AllComponentsEqual(playerVelHistory0Ws, kInvalidVector) && !AllComponentsEqual(playerVelHistory1Ws, kInvalidVector) && playerVelLen > 0.0001f)
		{
			// velocity magnitude prediction
			const float playerVelHistory1Len = Length(playerVelHistory1Ws);
			const float playerVelHistory0Len = Length(playerVelHistory0Ws);
			const Vector playerVelDirWs = playerVelWs / playerVelLen;

			CONST_EXPR float kH2Coef = -2.4f;
			CONST_EXPR float kH1Coef = 1.0f;
			CONST_EXPR float kH0Coef = 2.4f;

			const float playerPredVelLen = kH2Coef * playerVelHistory1Len + kH1Coef * playerVelHistory0Len + kH0Coef * playerVelLen;
			if (playerPredVelLen > 0.0f)
			{
				playerPredVelWs = playerPredVelLen * playerVelDirWs;
				if (playerVelHistory1Len > 0.0001f && playerVelHistory0Len > 0.0001f)
				{
					// if we have valid dirs for all, also do some dir prediction
					const Vector playerVelHistory1Dir = playerVelHistory1Ws / playerVelHistory1Len;
					const Vector playerVelHistory0Dir = playerVelHistory0Ws / playerVelHistory0Len;

					Vector playerPredDirWs = kH2Coef * playerVelHistory1Dir + kH1Coef * playerVelHistory0Dir + kH0Coef * playerVelDirWs;
					const float playerPredDirLen = Length(playerPredDirWs);
					playerPredDirWs /= playerPredDirLen;

					if (playerPredDirLen > 0.0001f && Dot(playerVelDirWs, playerPredDirWs) > 0.0f)
					{
						playerPredVelWs = playerPredVelLen * Slerp(playerVelDirWs, playerPredDirWs, 0.25f);
					}
				}
			}
			else
			{
				playerPredVelWs = kZero;
			}
		}

		CONST_EXPR float kMaxPredVel = 4.0f;
		if (LengthSqr(playerPredVelWs) > kMaxPredVel * kMaxPredVel)
			playerPredVelWs = kMaxPredVel * Normalize(playerPredVelWs);

		//g_prim.Draw(DebugArrow(playerPosWs + Vector(0.0f, 0.1f, 0.0f), playerVelHistory1Ws, kColorGreen), kPrimDuration1FramePauseable);
		//g_prim.Draw(DebugArrow(playerPosWs + Vector(0.0f, 0.1f, 0.0f), playerVelHistory0Ws, kColorRed), kPrimDuration1FramePauseable);
		//g_prim.Draw(DebugArrow(playerPosWs + Vector(0.0f, 0.1f, 0.0f), playerVelWs, kColorWhite), kPrimDuration1FramePauseable);
		//g_prim.Draw(DebugArrow(playerPosWs + Vector(0.0f, 0.1f, 0.0f), playerPredVelWs, kColorBlue), kPrimDuration1FramePauseable);

		const Vector stickWs = pPlayer->IsState(SID("Script")) ? kZero : pPlayer->GetStickWS(NdPlayerJoypad::kStickMove);
		const float stickLen = Length(stickWs);

		const Vector camWs = AsUnitVectorXz(GetLocalZ(GameGlobalCameraLocator(0).GetRotation()), kZero);

		playerProbeStartWs = playerPosWs;

		//const bool isMelee = pPlayer->GetBuddyMeleePredictedIsMelee();
		haveMeleePred = !AllComponentsEqual(pPlayer->GetBuddyMeleePredictedWs(), kInvalidPoint);
		const Vector meleePredWs = VectorXz(pPlayer->GetBuddyMeleePredictedWs() - playerProbeStartWs);

		// potential prediction contributors:
		//   - playerVelWs
		//   - stickWs
		//   - meleePredWs
		//   - camWs

		Vector probeVecWs = 0.28f * playerPredVelWs + Max(Length(playerPredVelWs) > 0.05f ? 0.26f : 0.22f, 0.12f * Length(playerPredVelWs)) * stickWs;

		if (haveMeleePred)
		{
			if (LengthSqr(meleePredWs))
			{
				// the more stick we have in, the less we can rely on the melee predictor since things like dodges can be steered :(
				if (stickLen > 0.25f)
				{
					probeVecWs = 0.0f * probeVecWs + 0.22f * meleePredWs + 1.06f * stickWs;
				}
				else
				{
					probeVecWs = 0.30f * probeVecWs + 1.03f * meleePredWs;
				}
			}
			else
			{
				probeVecWs = 0.32f * playerPredVelWs;
			}
		}

		if (!AllComponentsEqual(stickWs, kZero) && LengthSqr(probeVecWs) > 0.0001f && Dot(Normalize(stickWs), Normalize(probeVecWs)) > 0.0f)
		{
			probeVecWs = Length(probeVecWs) * Slerp(Normalize(probeVecWs), Normalize(stickWs), 0.10f);
		}

		const Vector toHead = VectorXz(pPlayer->GetEyeWs().GetPosition() - playerPosWs);
		// if player is prone/crouched, bias toward head
		if (playerIsProne)
			probeVecWs += 0.71f * toHead;
		if (playerIsCrouched)
			probeVecWs += 0.21f * toHead;

		if (!AllComponentsEqual(camWs, kZero) && LengthSqr(probeVecWs) > 0.0001f && Dot(camWs, Normalize(probeVecWs)) > 0.0f)
		{
			probeVecWs = Length(probeVecWs) * Slerp(Normalize(probeVecWs), camWs, 0.08f);
		}

		// debug draw contributions
		if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_buddy.m_displayEvade))
		{
			const Vector yOfs(0.0f, 0.3f, 0.0f);
			g_prim.Draw(DebugArrow(playerProbeStartWs + yOfs, playerVelWs, kColorWhiteTrans, 0.4f), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugArrow(playerProbeStartWs + yOfs, playerPredVelWs, kColorWhiteTrans, 0.4f), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugArrow(playerProbeStartWs + yOfs + playerVelWs, playerProbeStartWs + yOfs + playerPredVelWs, kColorWhiteTrans, 0.4f), kPrimDuration1FramePauseable);

			g_prim.Draw(DebugArrow(playerProbeStartWs + yOfs, stickWs, kColorYellowTrans), kPrimDuration1FramePauseable);
			if (haveMeleePred)
				g_prim.Draw(DebugArrow(playerProbeStartWs + yOfs, meleePredWs, kColorGreenTrans), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugArrow(playerProbeStartWs + yOfs, camWs, kColorBlackTrans), kPrimDuration1FramePauseable);
		}

		playerProbeEndWs = playerProbeStartWs + probeVecWs;

		const NavLocation playerNavLoc = pPlayer->GetNavLocation();
		if (playerNavLoc.IsValid())
		{
			const NavMesh* pPlayerMesh = playerNavLoc.ToNavMesh();
			AI_ASSERT(pPlayerMesh);

			const NavPoly* pPlayerPoly = playerNavLoc.ToNavPoly();
			AI_ASSERT(pPlayerPoly);

			const Point probeStartPs = pPlayerMesh->WorldToParent(playerProbeStartWs);
			const Vector probeVecPs = pPlayerMesh->WorldToParent(probeVecWs);

			CONST_EXPR float kDegreeSpread = 6.5f;

			NavMesh::ProbeParams params1;
			params1.m_start = probeStartPs;
			params1.m_pStartPoly = pPlayerPoly;
			params1.m_move = RotateVectorAbout(probeVecPs, kUnitYAxis, DegreesToRadians(-kDegreeSpread));
			params1.m_probeRadius = 0.0f;
			params1.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskHuman;
			NavMesh::ProbeResult res1 = pPlayerMesh->ProbePs(&params1);
			const float dist1 = DistSqr(probeStartPs, params1.m_endPoint);
			//g_prim.Draw(DebugArrow(probeStartPs, params1.m_move, kColorWhite, 1.5f), kPrimDuration1FramePauseable);

			NavMesh::ProbeParams params2;
			params2.m_start = probeStartPs;
			params2.m_pStartPoly = pPlayerPoly;
			params2.m_move = RotateVectorAbout(probeVecPs, kUnitYAxis, DegreesToRadians(kDegreeSpread));
			params2.m_probeRadius = 0.0f;
			params2.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskHuman;
			NavMesh::ProbeResult res2 = pPlayerMesh->ProbePs(&params2);
			const float dist2 = DistSqr(probeStartPs, params2.m_endPoint);
			//g_prim.Draw(DebugArrow(probeStartPs, params2.m_move, kColorWhite, 1.5f), kPrimDuration1FramePauseable);

			NavMesh::ProbeParams params3;
			params3.m_start = probeStartPs;
			params3.m_pStartPoly = pPlayerPoly;
			params3.m_move = 1.001f * probeVecPs;
			params3.m_probeRadius = 0.0f;
			params3.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskHuman;
			NavMesh::ProbeResult res3 = pPlayerMesh->ProbePs(&params3);
			const float dist3 = DistSqr(probeStartPs, params3.m_endPoint);
			//g_prim.Draw(DebugArrow(probeStartPs, params3.m_move, kColorYellow, 1.5f), kPrimDuration1FramePauseable);

			playerProbeResWs = playerProbeStartWs;

			if (dist1 > dist2 && dist1 > dist3 && (res1 == NavMesh::ProbeResult::kHitEdge || res1 == NavMesh::ProbeResult::kReachedGoal))
			{
				const NavPoly* const probeResPoly = params1.m_pReachedPoly;
				if (probeResPoly)
					playerProbeResWs = params1.m_pReachedPoly->GetNavMesh()->ParentToWorld(params1.m_endPoint);
			}
			else if (dist2 > dist1 && dist2 > dist3 && (res2 == NavMesh::ProbeResult::kHitEdge || res2 == NavMesh::ProbeResult::kReachedGoal))
			{
				const NavPoly* const probeResPoly = params2.m_pReachedPoly;
				if (probeResPoly)
					playerProbeResWs = params2.m_pReachedPoly->GetNavMesh()->ParentToWorld(params2.m_endPoint);
			}
			else if (dist3 >= dist1 && dist3 >= dist2 && (res3 == NavMesh::ProbeResult::kHitEdge || res3 == NavMesh::ProbeResult::kReachedGoal))
			{
				const NavPoly* const probeResPoly = params3.m_pReachedPoly;
				if (probeResPoly)
					playerProbeResWs = params3.m_pReachedPoly->GetNavMesh()->ParentToWorld(params3.m_endPoint);
			}

			// preserve old Y though
			playerProbeResWs = PointFromXzAndY(playerProbeResWs, playerProbeStartWs);

			if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_buddy.m_displayEvade))
			{
				g_prim.Draw(DebugSphere(playerProbeResWs, 0.12f, kColorWhite), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugLine(playerProbeResWs, Vector(0.0f, 1.2f, 0.0f), kColorWhite, kColorWhite, 5.0f), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugString(playerProbeResWs + Vector(0.0f, 1.21f, 0.0f), "NavPred", 1, kColorWhite), kPrimDuration1FramePauseable);
			}
		}
		else
		{
			playerProbeResWs = playerProbeStartWs + 0.1f * probeVecWs;

			if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_buddy.m_displayEvade))
			{
				g_prim.Draw(DebugSphere(playerProbeResWs, 0.12f, kColorGray), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugLine(playerProbeResWs, Vector(0.0f, 1.2f, 0.0f), kColorGray, kColorGray, 5.0f), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugString(playerProbeResWs + Vector(0.0f, 1.21f, 0.0f), "OffNavPred", 1, kColorGray), kPrimDuration1FramePauseable);
			}
		}
	}
	AI_ASSERT(IsReasonable(playerProbeStartWs));
	AI_ASSERT(IsReasonable(playerProbeEndWs));
	AI_ASSERT(IsReasonable(playerProbeResWs));

	CONST_EXPR int kMaxNpcSegs = kMaxNpcCount;
	CONST_EXPR int kMaxObjectSegs = 2 * ObjectAvoidanceTracker::kMaxTrackedObjects;

	Segment playerAvoidSeg = { playerProbeStartWs, playerProbeResWs };

	Segment npcAvoidSegs[kMaxNpcSegs];
	bool npcsMoving[kMaxNpcSegs];
	int numAvoidNpcs = 0;

	// now for evading other npcs
	{
		const NpcSnapshot* pNpcsToAvoid[kMaxNpcSegs];

		{
			AtomicLockJanitorRead_Jls npcGroupLock(EngineComponents::GetNpcManager()->GetNpcGroupLock(), FILE_LINE_FUNC);
			for (NpcHandle hOtherNpc : EngineComponents::GetNpcManager()->GetNpcs())
			{
				const NpcSnapshot* const pOtherNpc = hOtherNpc.ToSnapshot<NpcSnapshot>();
				if (hOtherNpc == NavCharacterHandle(pNavChar))
					continue;

				if (pOtherNpc->IsDead())
					continue;

				if (pOtherNpc->IgnoredByNpcs())
					continue;

				if (IsEnemy(pNavChar->GetFactionId(), pOtherNpc->GetFactionId()))
					continue;

				if (DistSqr(pOtherNpc->GetTranslation(), meWs) > 4.0f)
					continue;

				if (!pOtherNpc->GetNavLocation().IsValid())
					continue;

				pNpcsToAvoid[numAvoidNpcs++] = pOtherNpc;
				AI_ASSERT(numAvoidNpcs <= kMaxNpcSegs);
			}
		}

		for (int i = 0; i < numAvoidNpcs; ++i)
		{
			const NpcSnapshot* const pAvoidNpc = pNpcsToAvoid[i];

			const NavLocation& avoidNpcNavLoc = pAvoidNpc->GetNavLocation();

			const NavMesh* pAvoidNpcMesh = avoidNpcNavLoc.ToNavMesh();
			AI_ASSERT(pAvoidNpcMesh);

			const NavPoly* pAvoidNpcPoly = avoidNpcNavLoc.ToNavPoly();
			AI_ASSERT(pAvoidNpcPoly);

			const Point probeStartWs = avoidNpcNavLoc.GetPosWs();
			Point probeResWs = probeStartWs;

			const Point probeStartPs = avoidNpcNavLoc.GetPosPs();
			const Vector probeVecPs = 0.60f * LimitVectorLength(pAvoidNpc->GetVelocityPs(), 0.0f, 3.0f);

			CONST_EXPR float kDegreeSpread = 7.5f;

			NavMesh::ProbeParams params1;
			params1.m_start = probeStartPs;
			params1.m_pStartPoly = pAvoidNpcPoly;
			params1.m_move = RotateVectorAbout(probeVecPs, kUnitYAxis, DegreesToRadians(-kDegreeSpread));
			params1.m_probeRadius = 0.0f;
			params1.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskHuman;
			NavMesh::ProbeResult res1 = pAvoidNpcMesh->ProbePs(&params1);
			const float dist1 = DistSqr(probeStartPs, params1.m_endPoint);
			//g_prim.Draw(DebugArrow(probeStartPs, params1.m_move, kColorWhite, 1.5f), kPrimDuration1FramePauseable);

			NavMesh::ProbeParams params2;
			params2.m_start = probeStartPs;
			params2.m_pStartPoly = pAvoidNpcPoly;
			params2.m_move = RotateVectorAbout(probeVecPs, kUnitYAxis, DegreesToRadians(kDegreeSpread));
			params2.m_probeRadius = 0.0f;
			params2.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskHuman;
			NavMesh::ProbeResult res2 = pAvoidNpcMesh->ProbePs(&params2);
			const float dist2 = DistSqr(probeStartPs, params2.m_endPoint);
			//g_prim.Draw(DebugArrow(probeStartPs, params2.m_move, kColorWhite, 1.5f), kPrimDuration1FramePauseable);

			NavMesh::ProbeParams params3;
			params3.m_start = probeStartPs;
			params3.m_pStartPoly = pAvoidNpcPoly;
			params3.m_move = 1.001f * probeVecPs;
			params3.m_probeRadius = 0.0f;
			params3.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskHuman;
			NavMesh::ProbeResult res3 = pAvoidNpcMesh->ProbePs(&params3);
			const float dist3 = DistSqr(probeStartPs, params3.m_endPoint);
			//g_prim.Draw(DebugArrow(probeStartPs, params3.m_move, kColorYellow, 1.5f), kPrimDuration1FramePauseable);

			if (dist1 > dist2 && dist1 > dist3 && (res1 == NavMesh::ProbeResult::kHitEdge || res1 == NavMesh::ProbeResult::kReachedGoal))
			{
				const NavPoly* const probeResPoly = params1.m_pReachedPoly;
				if (probeResPoly)
					probeResWs = params1.m_pReachedPoly->GetNavMesh()->ParentToWorld(params1.m_endPoint);
			}
			else if (dist2 > dist1 && dist2 > dist3 && (res2 == NavMesh::ProbeResult::kHitEdge || res2 == NavMesh::ProbeResult::kReachedGoal))
			{
				const NavPoly* const probeResPoly = params2.m_pReachedPoly;
				if (probeResPoly)
					probeResWs = params2.m_pReachedPoly->GetNavMesh()->ParentToWorld(params2.m_endPoint);
			}
			else if (dist3 >= dist1 && dist3 >= dist2 && (res3 == NavMesh::ProbeResult::kHitEdge || res3 == NavMesh::ProbeResult::kReachedGoal))
			{
				const NavPoly* const probeResPoly = params3.m_pReachedPoly;
				if (probeResPoly)
					probeResWs = params3.m_pReachedPoly->GetNavMesh()->ParentToWorld(params3.m_endPoint);
			}

			// preserve old Y though
			probeResWs = PointFromXzAndY(probeResWs, probeStartWs);

			npcAvoidSegs[i] = { probeStartWs, probeResWs };
			npcsMoving[i] = pAvoidNpc->IsMoving();

			if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_buddy.m_displayEvade))
			{
				const Color color = LumLerp(kColorGreen, kColorBlue, 0.5f);
				g_prim.Draw(DebugSphere(probeResWs, 0.12f, color), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugLine(probeResWs, Vector(0.0f, 1.2f, 0.0f), color, color, 5.0f), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugString(probeResWs + Vector(0.0f, 1.21f, 0.0f), "NpcPred", 1, color), kPrimDuration1FramePauseable);
			}
		}
	}

	// okay, we have a direct player segment and some number of npc and object segments to try to avoid.
	// let each discard more evade dirs, if applicable, and keep track of the ideal evade dir(s) of the first valid segment encountered to use later as a tiebreak

	struct AvoidSeg
	{
		Segment m_seg;
		EvadeType m_type;
		bool m_moving = true;
	};

	AvoidSeg avoidSegs[1 + kMaxNpcSegs + kMaxObjectSegs];

	// order of evaluation: moving player, objects, stationary player, npcs
	const bool playerStationary = DistXzSqr(playerProbeStartWs, playerProbeResWs) <= Sqr(0.01f);

	Segment objectSegs[kMaxObjectSegs];
	const int numAvoidObjects = g_objectAvoidanceTracker.GetAvoidSegs(objectSegs, kMaxObjectSegs, FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_buddy.m_displayEvade));

	int numTotalSegs = 0;

	// moving player
	if (!playerStationary)
	{
		avoidSegs[numTotalSegs++] = { playerAvoidSeg, kPlayer };
	}

	// objects
	for (int iObj = 0; iObj < numAvoidObjects; ++iObj)
	{
		avoidSegs[numTotalSegs++] = { objectSegs[iObj], kObject };
	}

	// stationary player
	if (playerStationary)
	{
		avoidSegs[numTotalSegs++] = { playerAvoidSeg, kPlayer };
	}

	// npcs
	for (int iNpc = 0; iNpc < numAvoidNpcs; ++iNpc)
	{
		avoidSegs[numTotalSegs++] = { npcAvoidSegs[iNpc], kNpc, npcsMoving[iNpc] };
	}

	Vector idealEvadeDir1Ws = kInvalidVector;
	Vector idealEvadeDir2Ws = kInvalidVector;
	int iPrimarySeg = -1;
	EvadeType primarySegType = kTypeCount;
	Vector secondaryIdealEvadeDir = kInvalidVector;
	U32 newEvadeThreatsMask = 0U;
	for (int iSeg = 0; iSeg < numTotalSegs; ++iSeg)
	{
		// if we have restarted evaluation after finding our primary seg
		// don't re-run evaluation on that seg
		if (iSeg == iPrimarySeg)
			continue;

		const AvoidSeg& avoidSeg = avoidSegs[iSeg];

		const bool playerSeg = avoidSeg.m_type == kPlayer;
		const bool npcSeg = avoidSeg.m_type == kNpc;
		const bool objectSeg = avoidSeg.m_type == kObject;

		const Point probeStartWs = avoidSeg.m_seg.a;
		const Point probeResWs = avoidSeg.m_seg.b;

		const Vector probeResVec = probeResWs - probeStartWs;

		if (Dist(meTestWs, probeStartWs) < 0.01f)
		{
			RejectEvade(pNavChar, probeStartWs, probeResWs, "kStartedOnMe");
			continue;
		}

		const Vector probeResVecXz = VectorXz(probeResVec);
		const float probeResVecXzLen = Length(probeResVecXz);
		const Vector probeResXzDir = probeResVecXz / probeResVecXzLen;
		const float probeResVecYLen = Abs(probeResVec).Y();
		const Vector probeResToMeWs = VectorXz(meTestWs - probeResWs);
		const float probeResToMeWsLen = Length(probeResToMeWs);
		const float probeResAngle = RADIANS_TO_DEGREES(Atan2(probeResVecYLen, probeResVecXzLen));
		const bool mostlyVertical = probeResAngle > 70.0f;

		// if we're moving away already, don't bother evading
		if (!objectSeg && meSpeed > 0.6f)
		{
			if (meIsMoving)
			{
				if (mostlyVertical)
				{
					if (probeResToMeWsLen > 0.01f)
					{
						const float dot = DotXz(probeResToMeWs / probeResToMeWsLen, meDirWs);
						if (dot > 0.6f)
						{
							RejectEvade(pNavChar, probeStartWs, probeResWs, "kMovingAway");
							continue;
						}
					}
				}
				else
				{
					if (probeResVecXzLen > 0.01f)
					{
						const float dot = DotXz(probeResXzDir, meDirWs);
						if (dot > 0.6f)
						{
							RejectEvade(pNavChar, probeStartWs, probeResWs, "kMovingAway");
							continue;
						}
					}
				}
			}
		}

		Segment testSeg;
		if (m_dog)
		{
			const Vector yOfs(0.0f, 0.5f, 0.0f);
			testSeg = { meTestWs + yOfs - 0.28f * meFwdWs, meTestWs + yOfs + 0.27f * meFwdWs };
		}
		else
		{
			testSeg = { meTestWs, meTestWs + Vector(0.0f, 1.6f, 0.0f) };
		}

		Point closestWs;
		float closestDist;
		{
			Scalar closestT;
			Scalar unused;
			closestDist = DistSegmentSegment({ probeStartWs, probeResWs }, testSeg, closestT, unused);

			if (closestT == 0.0f && probeResVecXzLen > 0.01f)
			{
				if (mostlyVertical || DotXz(probeResXzDir, AsUnitVectorXz(meTestWs - probeStartWs, kZero)) < (iPrimarySeg == -1 ? -0.05f : -0.50f))
				{
					RejectEvade(pNavChar, probeStartWs, probeResWs, "kThreatMovingAway");
					continue;
				}
			}

			closestWs = Lerp(probeStartWs, probeResWs, closestT);
		}

		if (!playerSeg && !npcSeg && probeResVecXzLen < 0.36f && probeStartWs.Y() < meWs.Y() + 0.3f && probeResWs.Y() < meWs.Y() + 0.3f && (DistSqr(probeStartWs, probeResWs) < Sqr(2.0f) || probeResWs.Y() < probeStartWs.Y() - 0.50f))
		{
			RejectEvade(pNavChar, probeStartWs, probeResWs, "kLowAndSlow");
			continue;
		}

		Point closestExtendedWs;
		float closestExtendedDist, closestExtendedDistXz;
		{
			Point closestExtendedOnCharWs;
			closestExtendedDist = DistSegmentSegment({ probeStartWs, probeResWs + probeResVec }, testSeg, closestExtendedWs, closestExtendedOnCharWs);
			Scalar unused1, unused2;
			if (DistXzSqr(testSeg.a, testSeg.b))
				closestExtendedDistXz = DistSegmentSegmentXz({ probeStartWs, probeResWs + probeResVec }, testSeg, unused1, unused2);
			else
				closestExtendedDistXz = DistPointSegmentXz(testSeg.a, probeStartWs, probeResWs + probeResVec);
		}

		if (closestDist >= (iPrimarySeg == -1 ? (playerSeg ? (playerIsProne ? 0.65f : playerIsCrouched ? 0.61f : 0.61f) : (npcSeg ? (aliceFetch ? 0.75f : 0.61f) : (mostlyVertical ? 0.62f : 0.48f))) : (npcSeg ? 2.5f : playerSeg ? 2.2f : 1.95f)))
		{
			RejectEvade(pNavChar, probeStartWs, probeResWs, "kFar");
			continue;
		}

		if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_buddy.m_displayEvade))
		{
			const Vector yOfs = playerSeg ? Vector(0.0f, 0.6f, 0.0f) : npcSeg ? Vector(0.0f, 0.1f, 0.0f) : Vector(0.0f, 0.01f, 0.0f);
			g_prim.Draw(DebugLine(probeStartWs + yOfs, probeResWs + yOfs, kColorBlue, kColorBlue, 4.0f), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugArrow(probeStartWs + yOfs, probeResWs + yOfs, kColorBlue), kPrimDuration1FramePauseable);

			g_prim.Draw(DebugSphere(closestWs, 0.08f, kColorPurple), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugSphere(closestExtendedWs, 0.09f, kColorOrange), kPrimDuration1FramePauseable);
		}

		if (IsEvading())
		{
			if (TestBit(m_evadeThreatsMask, kPlayer))
			{
				// currently evading player
				// means we should skip evading anything else (player is most important)
				RejectEvade(pNavChar, probeStartWs, probeResWs, "kEvadingPlayer");
				continue;
			}

			if (TestBit(m_evadeThreatsMask, kObject))
			{
				// currently evading object
				// means we should skip evading objects or npcs, but not players
				if (objectSeg || npcSeg)
				{
					RejectEvade(pNavChar, probeStartWs, probeResWs, "kEvadingObject");
					continue;
				}
			}

			if (TestBit(m_evadeThreatsMask, kNpc))
			{
				// currently evading npc
				// means we should skip evading objects or npcs, but not players
				if (objectSeg || npcSeg)
				{
					RejectEvade(pNavChar, probeStartWs, probeResWs, "kEvadingNpc");
					continue;
				}
			}
		}

		/// this is a segment we would like to evade.
		if (playerSeg)
			SetBit(newEvadeThreatsMask, kPlayer);
		if (npcSeg)
			SetBit(newEvadeThreatsMask, kNpc);
		if (objectSeg)
			SetBit(newEvadeThreatsMask, kObject);

		// 2 vectors to consider now:
		// - me to closest (if player) or closest extended. whatever.
		// - me to start of probe (i.e. player, or throwable, or whatever, current position)
		//
		// and 1 flag:
		// - mostly vertical?
		//
		// from this we get 2 conditions to care about:
		// - closest is almost on top of me?
		// - mostly vertical?
		//
		// therefore we have 4 distinct cases:
		// - super close to me and vertical
		// - super close to me and horizontal
		// - farther from me and vertical
		// - farther from me and horizontal
		//
		// for each case, we consider 2 effects:
		// - what directions should we invalidate?
		// - what 1 or 2 directions are ideal evade dirs?
		//
		// okay, considering each case in full:
		const char* evadeCase = nullptr;
		Vector evadeDir1Ws, evadeDir2Ws;
		if (mostlyVertical || probeResVecXzLen <= 0.01f)
		{
			if (closestExtendedDistXz < 0.25f && probeResVecXzLen > 0.01f)
			{
				evadeCase = "Near|Vert";
				// super close to me and vertical
				// no invalidation
				//
				// any ideal evade dir (try angled away), don't slerp back to probe
				const Vector meTestToClosestExtendedDir = AsUnitVectorXz(closestExtendedWs - meTestWs, kUnitZAxis);
				evadeDir1Ws = RotateVectorAbout(-meTestToClosestExtendedDir, kUnitYAxis, DEGREES_TO_RADIANS(-45.0f));
				evadeDir2Ws = RotateVectorAbout(-meTestToClosestExtendedDir, kUnitYAxis, DEGREES_TO_RADIANS(45.0f));
			}
			else
			{
				evadeCase = probeResVecXzLen > 0.01f ? "Far|Vert" : "Stat";
				// farther from me and vertical, or stationary
				// invalidate narrow wedge toward closest, don't slerp back to probe
				const float meTestToClosestExtendedLen = DistXz(meTestWs, closestExtendedWs);
				const Vector meTestToClosestExtendedDir = AsUnitVectorXz(closestExtendedWs - meTestWs, kUnitZAxis);
				if (closestExtendedDistXz > 0.01f && meTestToClosestExtendedLen > 0.01f)
				{
					if (closestExtendedDistXz < 0.40f && mostlyVertical && !playerSeg)
					{
						// don't invalidate anything
					}
					else
					{
						for (int iTest = 0; iTest < kNumTests; ++iTest)
						{
							if (!evadeValid.IsBitSet(iTest))
								continue;
							const Vector animDirWs = evadeAnimDirWs[iTest];
							const float dotTowardClosest = DotXz(animDirWs, meTestToClosestExtendedDir);
							if (dotTowardClosest > 0.74f)
								evadeValid.ClearBit(iTest);
						}
					}

					// 1 ideal evade dir away from closest, don't slerp back to probe
					evadeDir1Ws = evadeDir2Ws = -meTestToClosestExtendedDir;
				}
				else
				{
					// any ideal evade dir (try angled away), don't slerp back to probe
					evadeDir1Ws = RotateVectorAbout(-meTestToClosestExtendedDir, kUnitYAxis, DEGREES_TO_RADIANS(-45.0f));
					evadeDir2Ws = RotateVectorAbout(-meTestToClosestExtendedDir, kUnitYAxis, DEGREES_TO_RADIANS(45.0f));
				}
			}
		}
		else
		{
			const float distMeToStart = DistXz(meTestWs, probeStartWs);
			const float slerpBackToProbe = playerSeg ?
				LerpScale(0.0f, 3.5f, haveMeleePred ? 0.48f : 0.26f, 0.1f, distMeToStart) :
				LerpScale(0.0f, 5.0f, 0.32f, 0.10f, distMeToStart);

			if (closestExtendedDistXz < (playerSeg || npcSeg ? 0.075f : 0.015f))
			{
				evadeCase = "Near|Horz";
				// super close to me and horizontal
				// invalidate toward and away from probe dir
				for (int iTest = 0; iTest < kNumTests; ++iTest)
				{
					if (!evadeValid.IsBitSet(iTest))
						continue;
					const Vector animDirWs = evadeAnimDirWs[iTest];
					const float dotAway = DotXz(animDirWs, probeResXzDir);
					if (playerSeg)
					{
						if (dotAway > 0.92f || dotAway < -0.25f)
							evadeValid.ClearBit(iTest);
					}
					else
					{
						if (dotAway > 0.84f || dotAway < -0.37f)
							evadeValid.ClearBit(iTest);
					}
				}

				// 2 ideal evades rotated 90 off probe dir, DO slerp back to probe
				evadeDir1Ws = RotateYMinus90(probeResXzDir);
				evadeDir2Ws = -evadeDir1Ws;
			}
			else
			{
				evadeCase = "Far|Horz";
				// farther from me and horizontal
				// invalidate wide wedge toward closest, slerp back to probe if player
				const float meTestToClosestExtendedLen = DistXz(meTestWs, closestExtendedWs);
				const Vector meTestToClosestExtendedDir = AsUnitVectorXz(closestExtendedWs - meTestWs, kUnitZAxis);
				if (meTestToClosestExtendedLen > 0.01f)
				{
					const Vector unwiseDir = playerSeg ? Slerp(meTestToClosestExtendedDir, -probeResXzDir, Limit01(0.62f * slerpBackToProbe)) : meTestToClosestExtendedDir;
					for (int iTest = 0; iTest < kNumTests; ++iTest)
					{
						if (!evadeValid.IsBitSet(iTest))
							continue;
						const Vector animDirWs = evadeAnimDirWs[iTest];
						const float dot = DotXz(animDirWs, unwiseDir);
						if (dot > (playerSeg ? -0.42f : LerpScale(0.0f, 1.0f, -0.05f, 0.35f, closestExtendedDistXz)))
							evadeValid.ClearBit(iTest);
					}

					// 1 ideal evade dir away from closest, DO slerp back to probe
					evadeDir1Ws = evadeDir2Ws = -meTestToClosestExtendedDir;
				}
				else
				{
					// any ideal evade dir (try angled away), don't slerp back to probe
					evadeDir1Ws = RotateVectorAbout(-meTestToClosestExtendedDir, kUnitYAxis, DEGREES_TO_RADIANS(-45.0f));
					evadeDir2Ws = RotateVectorAbout(-meTestToClosestExtendedDir, kUnitYAxis, DEGREES_TO_RADIANS(45.0f));
				}
			}

			if (probeResVecXzLen > 0.01f)
			{
				evadeDir1Ws = Slerp(evadeDir1Ws, probeResXzDir, slerpBackToProbe);
				evadeDir2Ws = Slerp(evadeDir2Ws, probeResXzDir, slerpBackToProbe);
			}
		}

		// hack check for alice fetch
		if (npcSeg && !aliceFetch)
			continue;

		if (playerSeg && !ShouldEvadePlayer(pNavChar, pPlayer))
			continue;

		if (npcSeg && !avoidSeg.m_moving)
		{
			RejectEvade(pNavChar, probeStartWs, probeResWs, "kNpcNotTryingToMove");
			continue;
		}

		AI_ASSERT(IsReasonable(evadeDir1Ws));
		AI_ASSERT(IsReasonable(evadeDir2Ws));

		AI_ASSERT(evadeCase);
		if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_buddy.m_displayEvade))
		{
			char s[64];
			sprintf(s, "%.2gm|%s", closestExtendedDistXz, evadeCase);
			g_prim.Draw(DebugString(Lerp(probeStartWs, probeResWs, 0.5f), s, 1, kColorGreen, 0.8f), kPrimDuration1FramePauseable);
		}

		// store off ideal dirs and restart evaluation if this is the first segment that has wanted evasion
		if (iPrimarySeg == -1)
		{
			AI_ASSERT(AllComponentsEqual(idealEvadeDir1Ws, kInvalidVector));
			AI_ASSERT(AllComponentsEqual(idealEvadeDir2Ws, kInvalidVector));

			iPrimarySeg = iSeg;
			primarySegType = playerSeg ? kPlayer : npcSeg ? kNpc : kObject;
			idealEvadeDir1Ws = evadeDir1Ws;
			idealEvadeDir2Ws = evadeDir2Ws;

			if (Npc* const pMutableNpc = Npc::FromProcess(GetCharacter()))
			{
				if (BuddyLookAtLogic* const pLogic = pMutableNpc->GetBuddyLookAtLogic())
				{
					if (playerSeg)
					{
						pLogic->NotifyLookAtPlayer(pMutableNpc, Seconds(1.2f), BuddyLookAtLogic::kLookPriorityHighest, BuddyLookAtLogic::kLookConeNone);
					}
					else
					{
						// I think it looks quite nice to just look along the prediction instead of tracking the object
						const Point lookPtWs = closestExtendedDist > 0.2f ? closestExtendedWs : Lerp(probeStartWs, probeResWs, 0.6f);
						if (lookPtWs.Y() > meWs.Y() - 2.5f)
						{
							pLogic->NotifyLookAtPoint(pMutableNpc, lookPtWs, Seconds(0.8f), BuddyLookAtLogic::kLookPriorityHighest, BuddyLookAtLogic::kLookConeNone);
						}
					}
				}
			}

			if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_buddy.m_displayEvade))
			{
				const Vector yOfs(0.0f, 0.05f, 0.0f);
				if (AllComponentsEqual(evadeDir1Ws, evadeDir2Ws))
				{
					g_prim.Draw(DebugString(meWs + yOfs + evadeDir1Ws, "IdealDir", 1, kColorPink, 0.8f), kPrimDuration1FramePauseable);
					g_prim.Draw(DebugArrow(meWs + yOfs, evadeDir1Ws, kColorPink), kPrimDuration1FramePauseable);
				}
				else
				{
					g_prim.Draw(DebugString(meWs + yOfs + evadeDir1Ws, "IdealDir1", 1, kColorPink, 0.75f), kPrimDuration1FramePauseable);
					g_prim.Draw(DebugArrow(meWs + yOfs, evadeDir1Ws, kColorPink), kPrimDuration1FramePauseable);
					g_prim.Draw(DebugString(meWs + yOfs + evadeDir2Ws, "IdealDir2", 1, kColorPink, 0.75f), kPrimDuration1FramePauseable);
					g_prim.Draw(DebugArrow(meWs + yOfs, evadeDir2Ws, kColorPink), kPrimDuration1FramePauseable);
				}
			}

			iSeg = -1;
		}
		else
		{
			// otherwise, accumulate secondary dir
			CONST_EXPR float kSecondaryDirSlerp = 0.15f;
			if (AllComponentsEqual(secondaryIdealEvadeDir, kInvalidVector))
			{
				secondaryIdealEvadeDir = evadeDir1Ws;
			}
			else
			{
				secondaryIdealEvadeDir = Slerp(secondaryIdealEvadeDir, evadeDir1Ws, kSecondaryDirSlerp);
			}
			if (!AllComponentsEqual(evadeDir1Ws, evadeDir2Ws))
			{
				secondaryIdealEvadeDir = Slerp(secondaryIdealEvadeDir, evadeDir2Ws, kSecondaryDirSlerp);
			}
			AI_ASSERT(IsReasonable(secondaryIdealEvadeDir));
			AI_ASSERT(Abs(Length(secondaryIdealEvadeDir) - 1.0f) < 0.01f);
		}
	}

	m_evadeThreatsMask |= newEvadeThreatsMask;

	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_buddy.m_displayEvade))
	{
		if (!AllComponentsEqual(secondaryIdealEvadeDir, kInvalidVector))
		{
			const Vector yOfs(0.0f, 0.05f, 0.0f);
			g_prim.Draw(DebugString(meWs + yOfs + secondaryIdealEvadeDir, "SecDir", 1, kColorPink, 0.8f), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugArrow(meWs + yOfs, secondaryIdealEvadeDir, kColorPink), kPrimDuration1FramePauseable);
		}
	}

	if (AllComponentsEqual(secondaryIdealEvadeDir, kInvalidVector))
		secondaryIdealEvadeDir = kZero;

	if (iPrimarySeg == -1)
	{
		//return RejectEvade(pNavChar, "kNoEvadeNeeded");
		return false;
	}

	// try to pick a valid evade that's closest to an ideal evade dir

	if (evadeValid.AreAllBitsClear())
	{
		bool flinched = false;

		// we're not gonna be able to evade. if not a player evade, try to play a flinch instead?
		if (primarySegType != kPlayer)
		{
			if (Human* const pHuman = Human::FromProcess(pNavChar))
			{
				flinched = pHuman->TryPlayFlinch(pHuman->GetTranslation() - 1.2f * idealEvadeDir1Ws);
			}
		}

		if (flinched)
			return RejectEvade(pNavChar, "kNoValidEvadeDirsButWillFlinch");
		else
			return RejectEvade(pNavChar, "kNoValidEvadeDirsNorFlinches");
	}

	// at least one valid evade
	int iBest = -1;
	float bestScore = -2.0f;
	for (int iTest = 0; iTest < kNumTests; ++iTest)
	{
		const bool valid = evadeValid.IsBitSet(iTest);

		if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_buddy.m_displayEvade))
		{
			const Color evadeColor = evadeDirs[iTest] == kForward
										 ? kColorWhite
										 : evadeDirs[iTest] == kBackward
											   ? kColorBlue
											   : evadeDirs[iTest] == kLeft ? kColorCyan : kColorMagenta;

			//g_prim.Draw(DebugArrow(meWs + Vector(0.0f, 0.05f, 0.0f), testDirWs, evadeColor), kPrimDuration1FramePauseable);
			//g_prim.Draw(DebugArrow(meWs + Vector(0.0f, 0.05f, 0.0f), alignWs.Pos(), evadeColor), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugArrow(meWs + Vector(0.0f, 0.05f, 0.0f), evadeFinalPosWs[iTest], valid ? evadeColor : Color(0.45f * evadeColor)), kPrimDuration1FramePauseable);
		}

		if (!valid)
			continue;

		const Vector animDirWs = evadeAnimDirWs[iTest];

		const float priDot = Max(Dot(idealEvadeDir1Ws, animDirWs), Dot(idealEvadeDir2Ws, animDirWs));
		const float secDot = Dot(secondaryIdealEvadeDir, animDirWs);
		const float curScore = Lerp(priDot, secDot, primarySegType == kPlayer || primarySegType == kNpc ? 0.2f : 0.5f);
		if (curScore > bestScore)
		{
			bestScore = curScore;
			iBest = iTest;
		}
	}

	AI_ASSERT(iBest != -1);

	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_buddy.m_displayEvade))
	{
		g_prim.Draw(DebugArrow(meWs + Vector(0.0f, 0.12f, 0.0f), evadeFinalPosWs[iBest] - meWs, kColorGreen), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugString(evadeFinalPosWs[iBest] + Vector(0.0f, 0.12f, 0.0f), "Decision", 1, kColorGreen, 0.8f), kPrimDuration1FramePauseable);
	}

	AiLogAnim(pNavChar, "Evading! %s\n", apEvadeAnims[iBest]->GetName());

	AI::SetPluggableAnim(pNavChar, apEvadeAnims[iBest]->GetNameId());

	BoundFrame evadeBf = pNavChar->GetBoundFrame();
	evadeBf.SetLocatorWs(evadeApRefWs[iBest]);

	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	FadeToStateParams params;
	params.m_apRef = evadeBf;
	params.m_apRefValid = true;
	params.m_animFadeTime = 0.4f;
	params.m_motionFadeTime = 0.2f;

	m_evadeAction.FadeToState(pAnimControl,
							  SID("s_evade-state"),
							  params,
							  AnimAction::kFinishOnAnimEndEarly);

	NavAnimHandoffDesc handoff;
	handoff.SetStateChangeRequestId(m_evadeAction.GetStateChangeRequestId(), SID("s_evade-state"));
	handoff.m_motionType = kMotionTypeMax;
	pNavChar->ConfigureNavigationHandOff(handoff, FILE_LINE_FUNC);

	pNavChar->OnSuccessfulEvade();

	return true;
}
