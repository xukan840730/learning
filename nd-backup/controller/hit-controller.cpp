/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/hit-controller.h"

#include "corelib/util/msg-mem.h"
#include "corelib/util/random.h"
#include "corelib/util/user.h"

#include "ndlib/anim/anim-action.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-util.h"
#include "ndlib/anim/feather-blend-table.h"
#include "ndlib/anim/hand-fix-ik-plugin.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/netbridge/mail.h"
#include "ndlib/render/util/text-printer.h"

#include "gamelib/anim/gesture-controller.h"
#include "gamelib/audio/nd-vox-controller-base.h"
#include "gamelib/debug/ai-msg-log.h"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/character-melee-impulse.h"
#include "gamelib/gameplay/nav/cover-action-pack.h"
#include "gamelib/gameplay/nav/nav-state-machine.h"
#include "gamelib/gameplay/nd-subsystem-mgr.h"
#include "gamelib/gameplay/nd-subsystem.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/ndphys/composite-body.h"
#include "gamelib/ndphys/composite-keyframe-controller.h"
#include "gamelib/ndphys/composite-ragdoll-controller.h"
#include "gamelib/ndphys/debugdraw/havok-debug-draw.h"
#include "gamelib/ndphys/havok-util.h"
#include "gamelib/state-script/ss-manager.h"

#include "ailib/util/potential-targets.h"

#include "game/ai/agent/npc-action-pack-util.h"
#include "game/ai/agent/npc.h"
#include "game/ai/base/ai-game-debug.h"
#include "game/ai/characters/infected.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/look-aim/look-aim-basic.h"
#include "game/ai/reaction/death-hint-mgr.h"
#include "game/ai/skill/skill.h"
#include "game/audio/speaker-selectors.h"
#include "game/event-attack.h"
#include "game/player/melee/melee-util.h"
#include "game/player/melee/process-melee-action.h"
#include "game/player/player.h"
#include "game/scriptx/h/anim-npc-info.h"
#include "game/vehicle/driveable-vehicle.h"
#include "game/vehicle/rail-controller.h"
#include "game/vehicle/rail-vehicle.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static const U32F kInvalidReactionIndex = 0xFFFF;
static const DC::HitReactionStateMask kArmorMask = (DC::kHitReactionStateMaskArmorMasterDestroyed
													| DC::kHitReactionStateMaskArmorDestroyed
													| DC::kHitReactionStateMaskArmor);
static const DC::HitReactionStateMask kLimbMask	 = (DC::kHitReactionStateMaskLegless);
static const DC::HitReactionStateMask kMeleeHrFilterMask   = (DC::kHitReactionStateMaskMeleeHrFiltered);
static const DC::HitReactionStateMask kSelectiveFilterMask = (DC::kHitReactionStateMaskLethalDismemberment
															  | DC::kHitReactionStateMaskFullHealth);
static const TimeFrame kHitReactionCoverReservationTime	   = Seconds(5.0f);

/// --------------------------------------------------------------------------------------------------------------- ///
struct BiasEntry
{
	U32 m_bias;
	U32 m_index;
	float m_poseMatchDistErr;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static I32 ComparePotentialBias(const BiasEntry& a, const BiasEntry& b)
{
	const float aDistErr = a.m_poseMatchDistErr < 0.0f ? kLargeFloat : a.m_poseMatchDistErr;
	const float bDistErr = b.m_poseMatchDistErr < 0.0f ? kLargeFloat : b.m_poseMatchDistErr;

	if (a.m_bias > b.m_bias)
		return -1;
	else if (a.m_bias < b.m_bias)
		return +1;
	else if (aDistErr < bDistErr)
		return -1;
	else if (aDistErr > bDistErr)
		return +1;
	else
		return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct AiHitController : public IAiHitController
{
public:
	AiHitController();
	virtual ~AiHitController() override {}

	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override;
	virtual void NotifyDcRelocated(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual void RequestEmotionState(float phase, const DC::EmotionRangeArray* pEmotionRange);
	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;
	virtual bool IsBusy() const override;
	virtual bool ShouldInterruptNavigation() const override;
	virtual bool ShouldInterruptSkills() const override;

	virtual bool TakeHit(const HitDescription* pHitDesc) override;
	bool TakeHitInternal(const HitDescription* pHitDesc, ProcessMeleeAction*& pRemoveMeleeAction, Npc*& pRemoveMeleePartner);
	virtual void Die(const HitDescription& hitDesc, bool fromScript = false) override;
	virtual void DieSpecial(StringId64 deathAnim,
							const BoundFrame* pApOverride = nullptr,
							StringId64 customApRefId	  = INVALID_STRING_ID_64,
							float fadeTime = -1.0f) override;
	virtual void FadeOutHitLayers(F32 fadeTime = 0.1f) override;
	virtual void FadeOutNonDeathLayers(F32 fadeTime = 0.1f) override;
	virtual void CancelAnimAction() override;
	virtual bool IsBlocking() const;

	virtual const TimeFrame GetLastHitTime() const override;
	virtual void SetLastHitTime(TimeFrame time) override;
	virtual bool IsHitReactionPlaying() const override;
	virtual bool IsExplosionFullBodyReactionPlaying() const override;
	virtual bool IsFullBodyReactionPlaying(DC::HitReactionStateMask mask = 0) const override;
	virtual bool IsStunReactionPlaying() const override;
	virtual bool IsStunKneeReactionPlaying() const override;
	virtual bool IsStunReactionFromThrownObjectPlaying() const override;
	virtual bool IsStunReactionFromBuddyThrownObjectPlaying() const override;
	virtual bool IsStunReactionFromNonBuddyThrownObjectPlaying() const override;
	virtual bool IsStunReactionFromSmokeOrStunBombPlaying() const override;
	virtual bool IsStunReactionFromPlayerSmokeOrStunBombPlaying() const override;
	virtual TimeFrame GetLastStunPlayingTime() const override;
	virtual float GetSecInStunReaction() const override;
	virtual bool IsInStrikePhaseRange() const override;
	virtual bool IsAdditiveHitReactionPlaying() const override;
	virtual StringId64 GetAdditiveAnim() const override;
	virtual bool IsAdditiveHitReactionPausingMelee() const override;
	virtual StringId64 GetOverrideHitPartial() const override;
	virtual void SetPartialOverride(StringId64 overridePartial, F32 frameStart) override;
	virtual bool IsFireBackGesturePlaying() const override;
	virtual bool Shove(Character* pShover, Point_arg originWs, Vector_arg motionDirWs) override;
	virtual bool Stun(float stunTime, DC::HitReactionSourceType hitReactionSourceType) override;
	virtual bool GoLegless(bool leftLeg, const AttackInfo* pAttackInfo) override;
	virtual bool IsBeingShoved() const override;
	virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const override;

	static void HandleNpcNpcCollision(NavCharacter* pCatcher, NavCharacter* pMover);
	static void PlayNpcStumble(NavCharacter* pCatcher, StringId64 anim, const BoundFrame& ap);
	virtual const TimeFrame GetTimeSinceLastReaction() const override
	{
		return GetProcessClock()->GetTimePassed(m_lastReactTime);
	}
	virtual void OverrideNextHitDirectionWs(Point_arg hitDirTargetWs) override;

	virtual bool WasKnockedDown() const override { return false; }
	virtual void KnockDown() override;

	virtual bool CanInitiateMeleeAttack() const override;

	virtual void NetHitReaction(StringId64 animListId,
								StringId64 bucketId,
								U32 animIndex,
								F32 startPhase,
								F32 playbackRate,
								TimeFrame startTime,
								const BoundFrame& boundFrame) override;
	virtual void NetDeathReaction(StringId64 animListId,
								  StringId64 bucketId,
								  U32 animIndex,
								  F32 exitPhase,
								  const BoundFrame& boundFrame,
								  bool mirror) override;
	virtual TimeFrame GetLastNetReactTime() override;

	virtual StringId64 GetLastMeleeHitReactionAnim() override;

	virtual bool PlayManualAdditiveHR(const StringId64 animId, const float strength, const bool mirror) override;

	virtual float GetGroundAdjustBlendFactor() const override { return m_groundAdjustFactor; }
	virtual float GetRagdollBlendOutHoleDepth() const override;

	NdRwAtomicLock64* GetLock() const { return &m_lock; }

	virtual bool IsLocked() override { return m_lock.IsLocked(); }

	void PostDeathAnim(StringId64 anim, StringId64 ragdollSettings) override;

	virtual bool DeathAnimWhitelistedForRagdollGroundProbesFix() const override;

private:
	struct PotentialHitAnim
	{
		const ArtItemAnim* m_pAnim;
		const DC::HitReactionEntry* m_pDcEntry;
		U32 m_dcIndex;
		float m_startPhase;
		float m_blendTime;
		float m_distErr;
		bool m_mirror;
		BoundFrame m_apRef;
	};

	struct BuildInfo
	{
		PoseMatchInfo m_baseMatchInfo;
		Locator m_lShoulderOs;
		Locator m_rShoulderOs;
		Locator m_rootOs;
		const HitDescription* m_pHitDesc;
		float m_curSpeed;
	};

	StringId64 GetCurrentActiveBucket() const;
	const DC::HitReactionEntryList* GetHitAnimationList(StringId64 setId, StringId64 bucketId) const;
	const DC::HitReactionEntryList* GetDeathAnimationList(StringId64 setId, StringId64 bucketId) const;

	bool AllHitAnimsDisabled(DC::HitReactionSourceType sourceType);

	enum class HandoffType
	{
		kNone,
		kIdle,
		kMoving
	};

	const char* GetHandoffTypeStr(HandoffType ht)
	{
		switch (ht)
		{
		case HandoffType::kNone: return "None";
		case HandoffType::kIdle: return "Idle";
		case HandoffType::kMoving: return "Moving";
		}

		return "???";
	}

	enum class AnimTypeFilter
	{
		kNone,
		kAdditiveOnly,
		kFullBodyOnly
	};

	struct PlayAnimParams
	{
		StringId64 m_animId		= INVALID_STRING_ID_64;
		BoundFrame m_frame		= BoundFrame(kIdentity);
		float m_startPhase		= 0.0f;
		float m_exitPhase		= 1.0f;
		float m_blendTime		= -1.0f;
		float m_motionBlendTime = -1.0f;
		float m_playbackRate	= 1.0f;
		StringId64 m_gestureId	= INVALID_STRING_ID_64;
		StringId64 m_dtGroupId	= INVALID_STRING_ID_64;
		StringId64 m_customApRefId = INVALID_STRING_ID_64;
		bool m_mirror		  = false;
		bool m_fromHorseDeath = false;
		HandoffType m_handoffType = HandoffType::kIdle;
		AnimAction* m_pAnimAction = nullptr;
	};

	bool PlayHitAnim(const PlayAnimParams& params);
	void PlayDeathAnim(const PlayAnimParams& params);
	void FadeOutAnim();

	StringId64 GetHitJointId(const HitDescription& hitDesc) const;
	DC::HitReactionDirectionMask GetHitDirection(const HitDescription& hitDesc) const;

	bool IsCoverRecoveryValid(const PotentialHitAnim& potentialHitAnim, const CoverActionPack* pCoverAp) const;

	U32F BuildPotentialHitAnimList(const DC::HitReactionEntryList* pHitList,
								   const HitDescription* pHitDesc,
								   AnimTypeFilter animTypeFilter,
								   float randomChanceVal,
								   PotentialHitAnim* pPotentialAnimsOut,
								   U32F maxAnimsOut,
								   bool inLastHitWindow,
								   bool lastHitWasAdditive) const;
	bool BuildPotentialHitAnimEntry(const DC::HitReactionEntry* pEntry,
									const U32F dcIndex,
									const BuildInfo& info,
									bool mirror,
									PotentialHitAnim* pPotentialAnimOut) const;
	U32F SelectHitReactionAnim(const PotentialHitAnim* pPotentials,
							   U32F numPotentials,
							   const CoverActionPack* pPanicCoverAp);
	AnimTypeFilter GetAnimTypeFilter(const HitDescription* pHitDesc) const;

	float GetDistanceErrorForAnim(const SkeletonId skelId,
								  const ArtItemAnim* pAnim,
								  float phase,
								  const Locator rootOs,
								  const Locator leftShOs,
								  const Locator rightShOs,
								  bool debugDraw = false) const;

	void FadeToDeathState(StringId64 deathAnimId, F32 animFadeTimeSeconds = 0.2f, F32 motionFadeTimeSeconds = 0.2f);

	const char* GetDirectionMaskName(U32F areaMask) const;
	const char* GetAttackTypeName(DC::HitReactionSourceType attackType) const;
	const char* GetNpcStateName(DC::HitReactionStateMask npcState) const;

	bool DieWithDeathHint();
	bool DieOnVehicleAsDriver();
	bool DieOnBoatHack(const HitDescription& hitDesc);
	virtual void DieOnHorse() override;

	float RateDeathHint(const DeathHint* pHint, BoundFrame* pApRefOut) const;
	void VisualizeHitReaction(const HitDescription* pHitDesc,
							  const PotentialHitAnim* pSelectedAnim,
							  float startPhase	 = -1.0f,
							  float playbackRate = -1.0f) const;

	Quat GetHitAnimApRefRotationWs(const PotentialHitAnim* pHitAnim, const HitDescription* pHitDesc) const;
	Point GetHitAnimApRefTranslationWs(const PotentialHitAnim* pPotential, Quat_arg apRefRotWs) const;

	bool StateMaskMatches(const DC::HitReactionStateMask npcState, const DC::HitReactionEntry* pDcEntry) const;
	bool HitJointIdMatches(const StringId64 hitJointId, const DC::HitReactionEntry* pDcEntry) const;
	bool HitEntryValidForCenterOfMass(const DC::HitReactionEntry* pDcEntry) const;
	bool DamageRangeMatches(const HitDescription* pHitDesc, const DC::HitReactionEntry* pDcEntry) const;
	bool StoppingPowerRangeMatches(const HitDescription* pHitDesc, const DC::HitReactionEntry* pDcEntry) const;

	void RememberHit(const HitDescription* pHitDesc);
	bool TestShotCountWindow(const DC::HitReactionShotWindow* pShotWindow) const;
	void ResetShotWindow();

	bool TestDistanceValid(const HitDescription* pHitDesc, const DC::AiRangeval* pRangeVal) const;
	bool TestPlayerDistanceValid(const HitDescription* pHitDesc, const DC::AiRangeval* pRangeVal) const;
	bool SpeedTestPasses(float curSpeed, const DC::AiRangeval* pRangeVal) const;

	struct SelectedRecovery
	{
		SelectedRecovery() : m_pDcDef(nullptr), m_startPhase(0.0f), m_skipPhase(-1.0f) {}

		const DC::HitReactionRecoveryEntry* m_pDcDef;
		ActionPackHandle m_hCoverAp;
		DC::SelfBlendParams m_selfBlend;
		BoundFrame m_initialAp;
		BoundFrame m_finalAp;
		float m_startPhase;
		float m_skipPhase;
	};

	void TryQueueHitReactionRecovery();
	SelectedRecovery PickHitReactionRecovery(const DC::HitReactionEntry* pEntry,
											 const CoverActionPack* pCoverApToTryFor) const;
	float RateRecovery_Idle(const DC::HitReactionRecoveryEntry& entry, SelectedRecovery* pRecoveryOut) const;
	float RateRecovery_Moving(const DC::HitReactionRecoveryEntry& entry, SelectedRecovery* pRecoveryOut) const;
	float RateRecovery_Cover(const DC::HitReactionRecoveryEntry& recovery,
							 const Locator& setupEndLocWs,
							 const CoverActionPack* pCoverAp,
							 SelectedRecovery* pRecoveryOut,
							 bool debugDraw) const;

	static float GetRandomChanceValue()
	{
		if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_randomChanceOverride >= 0.0f))
		{
			return g_aiGameOptions.m_hitReaction.m_randomChanceOverride;
		}

		return RandomFloatRange(0.0f, 1.0f);
	}

	void ValidateHitReactions() const override;
	void ValidateHitReactionsStateRange(const char* typeStr,
										DC::HitReactionStateMask startMask,
										DC::HitReactionStateMask endMask,
										DC::HitReactionStateMask maskIgnore,
										const StringId64 setId,
										const StringId64 bucketId,
										const DC::HitReactionEntryList* pHitList,
										const StringId64* pJointIdList,
										const U32F numJoints) const;
	void ValidateHitReactionsBucket(const char* typeStr,
									const StringId64 setId,
									const StringId64 bucketId,
									const DC::HitReactionEntryList* pHitList,
									const StringId64* pJointIdList,
									const U32F numJoints,
									const StringId64* armorList,
									const U32F numArmorPieces) const;
	void ValidateHitReactionsSet(const char* typeStr,
								 const StringId64 setId,
								 const StringId64* pJointIdList,
								 const U32F numJoints) const;

	void MailMissingHitReactionInfo(const HitDescription* pHitDesc, const char* typeStr, StringId64 setId) const;

	AnimActionWithSelfBlend m_fullBodyAnimAction;
	AnimAction m_exitAnimAction;
	AnimActionWithSelfBlend m_recoveryAnimAction;

	AnimAction m_partialAnimAction;
	AnimAction m_partialOverrideAnimAction;
	AnimAction m_additiveAnimAction;

	AnimAction m_shoveAnimAction;
	AnimAction m_knockDownAnimAction;
	AnimAction m_palletReactionAction;

	TimeFrame m_lastStunPlayingTime;
	TimeFrame m_lastHitTime;
	TimeFrame m_lastReactTime;
	TimeFrame m_lastNetReactTime;
	TimeFrame m_lastHitAnimTime;
	TimeFrame m_lastFullBodyHitAnimTime;
	TimeFrame m_deathAnimEndTime;
	TimeFrame m_stunAnimEndTime;

	float m_exitPhase;
	float m_groundAdjustFactor;
	float m_lastHitAdditiveWindowSec;

	StringId64 m_lastHitReactionBucket;
	const DC::HitReactionEntry* m_pLastSelectedHitEntry;
	const DC::HitReactionEntry* m_pLastFullBodyHitEntry;
	DC::HitReactionStateMask m_lastFullBodyHitReactionState; // Actual state of the NPC when HR was selected
	float m_lastFullBodyFadeOutPhase;
	Point m_hitDirOverrideTargetPs;

	Locator m_fullBodyExitAlignPs;

	SelectedRecovery m_hitRecovery;

	ActionPackHandle m_hRecoveryAp;

	StringId64 m_lastHitReactionMeleeAnim;

	DC::HitReactionSourceType m_lastFullBodySourceType;

	mutable NdRwAtomicLock64 m_lock;

	StringId64 m_lastExplosionReactionAnim;

	static CONST_EXPR size_t kShotHistorySize = 16;
	TimeFrame m_shotHistory[kShotHistorySize];

	HandFixIkPluginCallbackArg m_handFixIkArg;
	StringId64 m_partialOverride;
	F32 m_partialOverrideFrame;

	I32 m_featherBlendIndex;
	I32 m_featherBlendOutIndex;
	F32 m_partOverrideBlendInTime;
	F32 m_partOverrideBlendOutTime;

	StringId64 m_ragdollSettings;
	I32 m_ragdollSettingsDelay;

	StringId64 m_postDeathAnim;
	I32 m_postDeathAnimDelay;

	bool m_ragdollOnAnimEnd;
	bool m_interruptNavigation;
	bool m_interruptSkills;
	bool m_allowNpcCollision;
	bool m_hitDirOverrideValid;
	bool m_lastReactionWasBlock;
	bool m_ragdollKeepMotors;
	bool m_hitRecoverySelected;
	bool m_turnOffMotorsAtAnimEnd;
	bool m_lastFullBodyFromPlayer;

	DC::AiInfectedLeglessHitReaction m_leglessHitReaction;
};

/// --------------------------------------------------------------------------------------------------------------- ///
AiHitController::AiHitController()
	: m_partialAnimAction(SID("hit-reaction-partial"))
	, m_additiveAnimAction(SID("hit-reaction-additive"))
	, m_partialOverrideAnimAction(SID("hit-reaction-partial-override"))
	, m_ragdollOnAnimEnd(false)
	, m_interruptNavigation(false)
	, m_interruptSkills(false)
	, m_pLastSelectedHitEntry(nullptr)
	, m_pLastFullBodyHitEntry(nullptr)
	, m_lastFullBodyHitReactionState(0)
	, m_lastHitReactionBucket(INVALID_STRING_ID_64)
	, m_allowNpcCollision(false)
	, m_hitDirOverrideValid(false)
	, m_hitDirOverrideTargetPs(kOrigin)
	, m_exitPhase(-1.0f)
	, m_groundAdjustFactor(1.0f)
	, m_ragdollKeepMotors(false)
	, m_featherBlendIndex(-1)
	, m_turnOffMotorsAtAnimEnd(false)
	, m_ragdollSettings(INVALID_STRING_ID_64)
	, m_ragdollSettingsDelay(0)
	, m_postDeathAnim(INVALID_STRING_ID_64)
	, m_postDeathAnimDelay(0)
	, m_featherBlendOutIndex(-1)
	, m_lastFullBodySourceType(DC::kHitReactionSourceTypeMax)
	, m_partialOverride(INVALID_STRING_ID_64)
	, m_lastFullBodyFadeOutPhase(-1.0f)
	, m_fullBodyExitAlignPs(kOrigin)
	, m_lastFullBodyFromPlayer(false)
{
	m_partialOverride = INVALID_STRING_ID_64;
	ResetShotWindow();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::Init(NavCharacter* pNavChar, const NavControl* pNavControl)
{
	IAiHitController::Init(pNavChar, pNavControl);

	m_stunAnimEndTime	   = TimeFramePosInfinity();
	m_lastReactionWasBlock = false;
	m_lastExplosionReactionAnim = INVALID_STRING_ID_64;
	m_interruptNavigation		= false;
	m_interruptSkills	  = false;
	m_ragdollOnAnimEnd	  = false;
	m_hitRecoverySelected = false;
	m_lastNetReactTime	  = Seconds(0.0f);
	m_lastStunPlayingTime = TimeFrameNegInfinity();
	m_lastHitTime		  = TimeFrameNegInfinity();
	m_lastHitAnimTime	  = TimeFrameNegInfinity();
	m_lastFullBodyHitAnimTime  = TimeFrameNegInfinity();
	m_lastHitAdditiveWindowSec = -1.0f;
	m_deathAnimEndTime		   = TimeFrameNegInfinity();
	m_lastHitReactionMeleeAnim = INVALID_STRING_ID_64;
	ResetShotWindow();

	const FgAnimData* pAnimData = pNavChar->GetAnimData();

	m_featherBlendIndex	   = g_featherBlendTable.LoginFeatherBlend(SID("*melee-hit-reaction-partial*"), pAnimData);
	m_featherBlendOutIndex = g_featherBlendTable.LoginFeatherBlend(SID("*melee-hit-reaction-partial-blend-out*"),
																   pAnimData);

	if (const FeatherBlendTable::Entry* pEntry = g_featherBlendTable.GetEntry(m_featherBlendIndex))
	{
		m_partOverrideBlendInTime  = pEntry->m_blendTime;
		m_partOverrideBlendOutTime = pEntry->m_blendTime;
	}
	else
	{
		m_partOverrideBlendInTime  = 0.1f;
		m_partOverrideBlendOutTime = 0.1f;
	}

	m_handFixIkArg.m_tt = 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static CoverActionPack* GetBuddyCoverDiveAp(const Npc* pNpc, AiPost& post)
{
	post = pNpc->GetAssignedPost();
	if (post.IsValid() && post.GetType() == DC::kAiPostTypeCover)
	{
		if (const ActionPack* pAp = post.GetActionPack())
		{
			if (pAp->GetType() == ActionPack::kCoverActionPack)
			{
				const CoverActionPack* pCoverAp	   = (CoverActionPack*)pAp;
				CONST_EXPR float kMaxCoverDiveDist = 2.8f;
				if (DistSqr(pCoverAp->GetDefensivePosWs(), pNpc->GetTranslation())
					< kMaxCoverDiveDist * kMaxCoverDiveDist)
				{
					return (CoverActionPack*)pAp;
				}
			}
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::NotifyDcRelocated(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pLastFullBodyHitEntry, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pLastSelectedHitEntry, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiHitController::GetHitJointId(const HitDescription& hitDesc) const
{
	if (hitDesc.m_helmetKnockedOff)
		return SID("targHelmet");

	return hitDesc.m_hitAttachName;
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::HitReactionDirectionMask AiHitController::GetHitDirection(const HitDescription& hitDesc) const
{
	const Vector hitDirectionWs = hitDesc.m_directionWs;
	const Point impactPointWs	= hitDesc.m_impactPointWs;

	if (FALSE_IN_FINAL_BUILD(
			hitDesc.m_sourceType != DC::kHitReactionSourceTypeFlame 
			&& hitDesc.m_sourceType != DC::kHitReactionSourceTypeInfectedCloudInside 
			&& hitDesc.m_sourceType != DC::kHitReactionSourceTypeMelee))
	{
		ASSERT(Length(hitDirectionWs) > 0.95f && Length(hitDirectionWs) < 1.05f);
	}

	const NavCharacter* pNavChar   = GetCharacter();
	const Vector flattenedHitDirWs = SafeNormalize(VectorXz(hitDirectionWs), kZero);

	const Vector dirLs = pNavChar->GetLocator().UntransformVector(flattenedHitDirWs);

	const float hitAngleDeg = RADIANS_TO_DEGREES(Atan2(dirLs.X(), dirLs.Z()));

	DC::HitReactionDirectionMask classification = 0;

	// 	g_prim.Draw(DebugString(pNavChar->GetTranslation() + kUnitYAxis, StringBuilder<128>("%0.1f", hitAngleDeg).c_str()), Seconds(3.0f));
	// 	g_prim.Draw(DebugArrow(pNavChar->GetTranslation() + kUnitYAxis, flattenedHitDirWs, kColorRed, 0.5f, kPrimEnableHiddenLineAlpha), Seconds(3.0f));

	if ((hitAngleDeg <= -135.0f) || (hitAngleDeg >= 135.0f))
	{
		classification |= DC::kHitReactionDirectionMaskFront90;
	}
	else if ((hitAngleDeg <= 45.0f) && (hitAngleDeg >= -45.0f))
	{
		classification |= DC::kHitReactionDirectionMaskBack90;
	}
	else if ((hitAngleDeg >= 45.0f) && (hitAngleDeg <= 135.0f))
	{
		classification |= DC::kHitReactionDirectionMaskRight90;
	}
	else
	{
		classification |= DC::kHitReactionDirectionMaskLeft90;
	}

	if ((hitAngleDeg < -90.0f) || (hitAngleDeg > 90.0f))
	{
		classification |= DC::kHitReactionDirectionMaskFront180;
	}
	else
	{
		classification |= DC::kHitReactionDirectionMaskBack180;
	}

	if (hitAngleDeg >= 0.0f)
	{
		classification |= DC::kHitReactionDirectionMaskRight180;
	}
	else
	{
		classification |= DC::kHitReactionDirectionMaskLeft180;
	}

	return classification;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AiHitController::BuildPotentialHitAnimList(const DC::HitReactionEntryList* pHitList,
												const HitDescription* pHitDesc,
												AnimTypeFilter animTypeFilter,
												float randomChanceVal,
												PotentialHitAnim* pPotentialAnimsOut,
												U32F maxAnimsOut,
												bool inLastHitWindow,
												bool lastHitWasAdditive) const
{
	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_hitReactionMode == AiGameOptions::kHitReactionModeNone))
		return 0;

	if (!pHitList)
		return 0;

	const Npc* pNpc = (const Npc*)GetCharacter();
	const AnimControl* pAnimControl = pNpc->GetAnimControl();
	const JointCache* pJointCache	= pAnimControl->GetJointCache();
	PoseMatchInfo matchInfo;
	const I32F rsIndex = pNpc->FindJointIndex(SID("r_shoulder"));
	const I32F lsIndex = pNpc->FindJointIndex(SID("l_shoulder"));

	if (rsIndex < 0 || lsIndex < 0)
		return 0;

	const Locator alignWs = pNpc->GetLocator();

	const Locator rootWs	= pJointCache->GetJointLocatorWs(0);
	const Locator leftShWs	= pJointCache->GetJointLocatorWs(lsIndex);
	const Locator rightShWs = pJointCache->GetJointLocatorWs(rsIndex);

	const Locator rootOs	= alignWs.UntransformLocator(rootWs);
	const Locator leftShOs	= alignWs.UntransformLocator(leftShWs);
	const Locator rightShOs = alignWs.UntransformLocator(rightShWs);

	matchInfo.m_entries[0].m_valid		= true;
	matchInfo.m_entries[0].m_channelId	= SID("root");
	matchInfo.m_entries[0].m_matchPosOs = rootOs.Pos();

	matchInfo.m_entries[1].m_valid		= true;
	matchInfo.m_entries[1].m_channelId	= SID("lShoulder");
	matchInfo.m_entries[1].m_matchPosOs = leftShOs.Pos();

	matchInfo.m_entries[2].m_valid		= true;
	matchInfo.m_entries[2].m_channelId	= SID("rShoulder");
	matchInfo.m_entries[2].m_matchPosOs = rightShOs.Pos();

	BuildInfo info;
	info.m_baseMatchInfo = matchInfo;
	info.m_rootOs		 = rootOs;
	info.m_lShoulderOs	 = leftShOs;
	info.m_rShoulderOs	 = rightShOs;
	info.m_pHitDesc		 = pHitDesc;
	info.m_curSpeed		 = Length(pNpc->GetVelocityPs());

	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_hitReactionMode
							 == AiGameOptions::kHitReactionModeIndexedOnly))
	{
		const U32F debugIndex = g_aiGameOptions.m_hitReaction.m_hitReactionIndex % pHitList->m_count;
		const bool mirror	  = (pHitList->m_array[debugIndex].m_flags & DC::kHitReactionFlagsMirror) != 0;
		if (BuildPotentialHitAnimEntry(&pHitList->m_array[debugIndex], debugIndex, info, mirror, &pPotentialAnimsOut[0]))
		{
			return 1;
		}
		else
		{
			return 0;
		}
	}

	U32F numPotentials = 0;

	const DC::NpcDemeanor curDemeanor = pNpc->GetCurrentDemeanor().ToI32();
	const StringId64 hitJointId		  = GetHitJointId(*info.m_pHitDesc);
	const DC::HitReactionDirectionMask dirMask = GetHitDirection(*info.m_pHitDesc);
	const DC::HitReactionSourceType attackType = info.m_pHitDesc->m_sourceType;
	const DC::HitReactionLimbMask limb		   = info.m_pHitDesc->m_limb;

	const Clock* pClock = pNpc->GetClock();

	const bool deadly = pHitDesc && (pHitDesc->m_normalizedHealth <= 0.0f);
	const bool stun	  = pHitDesc && pHitDesc->m_stun;

	const bool inAdditiveWindow =
		!deadly
		&& !stun
		&& m_lastHitAdditiveWindowSec > 0.0f
		&& !pClock->TimePassed(Seconds(m_lastHitAdditiveWindowSec), m_lastFullBodyHitAnimTime);

	for (U32F currentIndex = 0; currentIndex < pHitList->m_count; ++currentIndex)
	{
		if (numPotentials >= maxAnimsOut)
			break;

		const DC::HitReactionEntry& currentHitAnim = pHitList->m_array[currentIndex];

		if (inLastHitWindow)
		{
			if (lastHitWasAdditive)
			{
				if (!currentHitAnim.m_ignoreAdditiveHitTimeout && !currentHitAnim.m_ignoreHitTimeout)
					continue;
			}
			else
			{
				if (!currentHitAnim.m_ignoreHitTimeout)
					continue;
			}

		}

		if (currentHitAnim.m_animId == INVALID_STRING_ID_64)
			continue;

		if (currentHitAnim.m_randomChance >= 0.0f && currentHitAnim.m_randomChance > randomChanceVal)
			continue;

		const DC::NpcDemeanorMask requiredDem = currentHitAnim.m_requiredDemeanor;
		if ((requiredDem > 0) && !(requiredDem & (1UL << curDemeanor)))
			continue;

		if ((currentHitAnim.m_dirMask != 0) && (0 == (currentHitAnim.m_dirMask & dirMask)))
			continue;

		if ((currentHitAnim.m_limbMask != 0) && limb && (0 == (currentHitAnim.m_limbMask & limb)))
			continue;

		if ((currentHitAnim.m_sourceType != 0) && (0 == (currentHitAnim.m_sourceType & attackType)))
			continue;

		if (!StateMaskMatches(info.m_pHitDesc->m_npcState, &currentHitAnim))
		{
			continue;
		}

		const bool isAdditive = currentHitAnim.m_animType == DC::kHitReactionAnimTypeAdditive
								|| currentHitAnim.m_animType == DC::kHitReactionAnimTypePartial
								|| currentHitAnim.m_animType == DC::kHitReactionAnimTypePowerRagdoll
								|| currentHitAnim.m_animType == DC::kHitReactionAnimTypePowerRagdollAndAdditive;

		if (inAdditiveWindow && !isAdditive && !currentHitAnim.m_ignoreHitTimeout && !currentHitAnim.m_ignoreAdditiveHitTimeout)
			continue;

		if ((animTypeFilter == AnimTypeFilter::kAdditiveOnly) && !isAdditive)
			continue;

		if ((animTypeFilter == AnimTypeFilter::kFullBodyOnly) && isAdditive)
			continue;

		const bool fireBackOnly = currentHitAnim.m_flags & DC::kHitReactionFlagsFireBackOnly;
		if (fireBackOnly && !info.m_pHitDesc->m_tryToFireBack)
			continue;

		if (!DamageRangeMatches(info.m_pHitDesc, &currentHitAnim))
		{
			continue;
		}

		if (!StoppingPowerRangeMatches(info.m_pHitDesc, &currentHitAnim))
		{
			continue;
		}

		if (pHitDesc->m_shotForCenterMass && HitEntryValidForCenterOfMass(&currentHitAnim))
		{
			// valid, include it
		}
		else if (!HitJointIdMatches(hitJointId, &currentHitAnim))
		{
			continue;
		}

		if (!TestShotCountWindow(currentHitAnim.m_shotWindow))
		{
			continue;
		}

		if (!TestDistanceValid(info.m_pHitDesc, currentHitAnim.m_validDistRange))
		{
			continue;
		}

		if (!TestPlayerDistanceValid(info.m_pHitDesc, currentHitAnim.m_validPlayerDistRange))
		{
			continue;
		}

		if (!SpeedTestPasses(info.m_curSpeed, currentHitAnim.m_speedRange))
		{
			continue;
		}

		const bool mirror = (currentHitAnim.m_flags & DC::kHitReactionFlagsMirror) != 0;

		if (BuildPotentialHitAnimEntry(&currentHitAnim, currentIndex, info, mirror, &pPotentialAnimsOut[numPotentials]))
		{
			++numPotentials;
		}
	}

	if (info.m_pHitDesc->m_npcState & kSelectiveFilterMask)
	{
		bool filtering		 = false;
		int oldNumPotentials = numPotentials;

		for (int i = 0; i < oldNumPotentials; i++)
		{
			// Always play dismemberment anim if we have it, otherwise it's ok to default.
			if (pPotentialAnimsOut[i].m_pDcEntry->m_stateMask & kSelectiveFilterMask)
			{
				if (!filtering)
				{
					filtering	  = true;
					numPotentials = 0;
				}

				pPotentialAnimsOut[numPotentials++] = pPotentialAnimsOut[i];
			}
		}
	}

	return numPotentials;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::BuildPotentialHitAnimEntry(const DC::HitReactionEntry* pEntry,
												 const U32F dcIndex,
												 const BuildInfo& info,
												 bool mirror,
												 PotentialHitAnim* pPotentialAnimOut) const
{
	if (!pEntry || !pPotentialAnimOut)
		return false;

	const NavCharacter* pNavChar	= GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const StringId64 animId			= pEntry->m_animId;
	const float phaseMin	 = pEntry->m_startPhaseRange.m_val0;
	const float phaseMax	 = pEntry->m_startPhaseRange.m_val1;
	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animId).ToArtItem();
	const JointCache* pJointCache = pAnimControl->GetJointCache();

	const bool isPowerRagdoll	  = pEntry->m_animType == DC::kHitReactionAnimTypePowerRagdoll;
	const bool hasRagdollSettings = pEntry->m_ragdollSettings != INVALID_STRING_ID_64;

	const bool valid = (pAnim != nullptr) || (isPowerRagdoll && hasRagdollSettings);

	if (!valid)
	{
		MsgConScriptError("[%s] Can't find hit anim '%s'%s\n",
						  pNavChar->GetName(),
						  DevKitOnly_StringIdToString(animId),
						  isPowerRagdoll ? " or any power-ragdoll settings" : "");
		return false;
	}

	PoseMatchInfo matchInfo = info.m_baseMatchInfo;
	matchInfo.m_startPhase	= phaseMin;
	matchInfo.m_endPhase	= phaseMax;
	matchInfo.m_mirror		= mirror;

	const float bestStartPhase = (phaseMax > phaseMin) && pAnim
									 ? Limit(CalculateBestPoseMatchPhase(pAnim, matchInfo).m_phase, phaseMin, phaseMax)
									 : phaseMin;

	const float distErr = GetDistanceErrorForAnim(pNavChar->GetSkeletonId(),
												  pAnim,
												  bestStartPhase,
												  info.m_rootOs,
												  info.m_lShoulderOs,
												  info.m_rShoulderOs);

	float blendTime = g_aiGameOptions.m_hitReaction.m_hitReactionBlendTimeMax;

	if (distErr >= 0.0f)
	{
		blendTime = LerpScale(g_aiGameOptions.m_hitReaction.m_hitReactionBlendErrorMin,
							  g_aiGameOptions.m_hitReaction.m_hitReactionBlendErrorMax,
							  g_aiGameOptions.m_hitReaction.m_hitReactionBlendTimeMin,
							  g_aiGameOptions.m_hitReaction.m_hitReactionBlendTimeMax,
							  distErr);
	}

	if (pEntry->m_forceBlendTime >= 0.0f)
	{
		blendTime = pEntry->m_forceBlendTime;
	}

	pPotentialAnimOut->m_pDcEntry	= pEntry;
	pPotentialAnimOut->m_startPhase = bestStartPhase;
	pPotentialAnimOut->m_blendTime	= blendTime;
	pPotentialAnimOut->m_distErr	= distErr;
	pPotentialAnimOut->m_pAnim		= pAnim;
	pPotentialAnimOut->m_dcIndex	= dcIndex;
	pPotentialAnimOut->m_mirror		= mirror;

	BoundFrame apRef	   = pNavChar->GetBoundFrame();
	const Quat apRefRotWs  = GetHitAnimApRefRotationWs(pPotentialAnimOut, info.m_pHitDesc);
	const Point apRefPosWs = GetHitAnimApRefTranslationWs(pPotentialAnimOut, apRefRotWs);

	apRef.SetTranslationWs(apRefPosWs);
	apRef.SetRotationWs(apRefRotWs);

	pPotentialAnimOut->m_apRef = apRef;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AiHitController::SelectHitReactionAnim(const PotentialHitAnim* pPotentials,
											U32F numPotentials,
											const CoverActionPack* pPanicCoverAp)
{
	if (!pPotentials || (0 == numPotentials))
		return kInvalidReactionIndex;

	const NavCharacter* pNavChar	= GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const SkeletonId skelId			= pNavChar->GetSkeletonId();

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	BiasEntry* pBiases = NDI_NEW BiasEntry[numPotentials];
	U32F numBiases	   = 0;

	for (U32F i = 0; i < numPotentials; ++i)
	{
		if (!pPotentials[i].m_pDcEntry)
			continue;

		pBiases[numBiases].m_index = i;
		pBiases[numBiases].m_bias  = 1000;

		if (pPotentials[i].m_pDcEntry->m_recoveryList)
		{
			if (IsCoverRecoveryValid(pPotentials[i], pPanicCoverAp))
			{
				pBiases[numBiases].m_bias += 100;
			}
			else
			{
				// non-cover recovery, rate it DOWN
				pBiases[numBiases].m_bias -= 100;
			}
		}

		switch (pPotentials[numBiases].m_pDcEntry->m_animType)
		{
		case DC::kHitReactionAnimTypePartial:
		case DC::kHitReactionAnimTypeAdditive:
			pBiases[numBiases].m_bias -= 10;
			break;
		case DC::kHitReactionAnimTypePowerRagdoll:
		case DC::kHitReactionAnimTypePowerRagdollAndAdditive:
			pBiases[numBiases].m_bias -= 9;
			break;
		}

		if (HitEntryValidForCenterOfMass(pPotentials[numBiases].m_pDcEntry))
		{
			pBiases[numBiases].m_bias += 5;
		}

		if (pPotentials[numBiases].m_pDcEntry->m_shotWindow)
		{
			pBiases[numBiases].m_bias += 1;
		}

		pBiases[numBiases].m_poseMatchDistErr = pPotentials[numBiases].m_distErr;

		++numBiases;
	}

	if (numBiases == 0)
	{
		return kInvalidReactionIndex;
	}

	QuickSort(pBiases, numBiases, ComparePotentialBias);

	U32F rangeCount = 0;
	while ((pBiases[rangeCount].m_bias == pBiases[0].m_bias)
		   && (Abs(pBiases[rangeCount].m_poseMatchDistErr - pBiases[0].m_poseMatchDistErr)
			   < g_aiGameOptions.m_hitReaction.m_hitReactionDistErrEpsilon)
		   && (rangeCount < numBiases))
	{
		++rangeCount;
	}

	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_debugPrintPotentialHitAnims))
	{
		MsgAnim("=================================================================\n");
		MsgAnim("Potential Hit Anims:\n");
		for (U32F i = 0; i < numBiases; ++i)
		{
			const PotentialHitAnim& entry = pPotentials[pBiases[i].m_index];
			MsgAnim(" %s [%d] %s (dist err: %f)\n",
					i < rangeCount ? "***" : "",
					pBiases[i].m_bias,
					entry.m_pAnim ? entry.m_pAnim->GetName() : DevKitOnly_StringIdToString(entry.m_pDcEntry->m_animId),
					entry.m_distErr);
		}
		MsgAnim("=================================================================\n");
	}

	const U32F biasIndex = (rangeCount > 0) ? (urand() % rangeCount) : 0;

	return pBiases[biasIndex].m_index;
}

F32 g_powerRagdollHitImpulseTime  = 0.15f;
F32 g_powerRagdollHitTimeOut	  = 1.0f;
F32 g_powerRagdollHitVertMax	  = 0.0f;
F32 g_powerRagdollHitHorizMin	  = 0.10f;
F32 g_powerRagdollHitDirHorizFact = 1.0f;

void PowerRagdollHit(Character* pNpc, const HitDescription* pHitDesc, StringId64 ragdollSettings, F32 impulse)
{
	static ScriptPointer<DC::HashTable>
		s_attachPointToRagdollJointMap(SID("*attach-point-to-ragdoll-joint-map*"), SID("hit-reactions"));

	CompositeBody* pRagdoll = pNpc->GetRagdollCompositeBody();
	if (!pRagdoll)
		return;

	I32 jointIndex = -1;
	if (s_attachPointToRagdollJointMap)
	{
		const DC::RagdollJoint* pRagdollJoint = ScriptManager::HashLookup<DC::RagdollJoint>(s_attachPointToRagdollJointMap
																								.GetTyped(),
																							pHitDesc->m_hitAttachName);
		if (pRagdollJoint)
		{
			jointIndex = pNpc->GetAnimData()->FindJoint(pRagdollJoint->m_joint);
			if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_debugPowerRagdoll))
				MsgAnim("AttachName to ragdoll joint per dc map: %s -> %s\n",
						DevKitOnly_StringIdToString(pHitDesc->m_hitAttachName),
						DevKitOnly_StringIdToString(pRagdollJoint->m_joint));
		}
	}

	if (jointIndex < 0)
	{
		if (const RigidBody* pHitBody = pHitDesc->m_hHitBody.ToBody())
		{
			jointIndex = pHitBody->GetJointIndex();
			if (const ArtItemSkeleton* pAnimSkel = pNpc->GetAnimData()->m_animateSkelHandle.ToArtItem())
				MsgAnim("Hit joint to ragdoll joint: %s\n", pAnimSkel->m_pJointDescs[jointIndex].m_pName);
		}
	}

	if (jointIndex < 0)
	{
		AttachIndex aIndex;
		pNpc->GetAttachSystem()->FindPointIndexById(&aIndex, pHitDesc->m_hitAttachName);
		if (aIndex == AttachIndex::kInvalid)
		{
			MsgAnim("No attach point for hit name: %s\n", DevKitOnly_StringIdToString(pHitDesc->m_hitAttachName));
			return;
		}
		jointIndex = pNpc->GetAttachSystem()->GetJointIndex(aIndex);
		if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_debugPowerRagdoll))
		{
			if (const ArtItemSkeleton* pAnimSkel = pNpc->GetAnimData()->m_animateSkelHandle.ToArtItem())
				MsgAnim("AttachName to ragdoll joint: %s -> %s\n",
						DevKitOnly_StringIdToString(pHitDesc->m_hitAttachName),
						pAnimSkel->m_pJointDescs[jointIndex].m_pName);
		}
	}

	RigidBody* pRagdollBody = nullptr;
	if (jointIndex >= 0)
	{
		pRagdollBody = pRagdoll->FindBodyByJointIndex(jointIndex);
	}

	Point pnt  = pHitDesc->m_impactPointWs;
	Vector dir = pHitDesc->m_directionWs;

	if (!pRagdollBody)
	{
		// Just find the closest ragdoll body to the hit point
		HavokMarkForReadJanitor jj;
		F32 dist = 0.15f; // if nothing found closer than this, we will give up
		for (U32F ii = 0; ii < pRagdoll->GetNumBodies(); ii++)
		{
			RigidBody* pBody = pRagdoll->GetBody(ii);
			if (HavokGetClosestPointOnBody(pBody, pnt, dist, nullptr))
			{
				pRagdollBody = pBody;
			}
		}
		if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_debugPowerRagdoll))
		{
			if (const ArtItemSkeleton* pAnimSkel = pNpc->GetAnimData()->m_animateSkelHandle.ToArtItem())
			{
				if (jointIndex >= 0)
					MsgAnim("No ragdoll body at joint %s\n", pAnimSkel->m_pJointDescs[jointIndex].m_pName);
				if (pRagdollBody)
				{
					MsgAnim("Closest ragdoll body is %s\n",
							pAnimSkel->m_pJointDescs[pRagdollBody->GetJointIndex()].m_pName);
				}
			}
		}
	}

	if (!pRagdollBody)
	{
		if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_debugPowerRagdoll))
			MsgAnim("No ragdoll ragdoll body. Aborting ragdoll impulse.\n");
		return;
	}

	if (!pNpc->PhysicalizeRagdollWithTimeOut(g_powerRagdollHitTimeOut,
											 ragdollSettings != INVALID_STRING_ID_64
												 ? ragdollSettings
												 : SID("*man-hit-reactions-ragdoll-control-settings*")),
		0.0f)
	{
		if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_debugPowerRagdoll))
			MsgAnim("Failed to physicalize ragdoll with settings %s.\n", DevKitOnly_StringIdToString(ragdollSettings));
		return;
	}

	// For hits on spine and head we want more of lateral rotation and less vertical
	switch (pRagdollBody->GetJointSid().GetValue())
	{
	case SID_VAL("pelvis"):
	case SID_VAL("spineb"):
	case SID_VAL("spinec"):
	case SID_VAL("spined"):
	case SID_VAL("neck"):
	case SID_VAL("headb"):
		{
			// Rotation arm in Ls of the body joint
			Locator bodyLoc = pRagdollBody->GetLocator();

			Vector dirLs = Unrotate(bodyLoc.GetRotation(), dir);
			dirLs.SetZ(MinMax01(1.0f - g_powerRagdollHitDirHorizFact) * dirLs.Z());
			dirLs = SafeNormalize(dirLs, kZero);
			dir	  = Rotate(bodyLoc.GetRotation(), dirLs);

			Point com = pRagdollBody->GetRealCm();
			Point comOnImp = pnt + Dot(dir, com - pnt) * dir;
			Vector armLs   = comOnImp - com;
			armLs = Unrotate(bodyLoc.GetRotation(), armLs);

			// Adjust the arm
			F32 vertArm = armLs.Z();
			Vector horizVec = armLs;
			horizVec.SetZ(0.0f);
			Scalar scHorizArm;
			horizVec	 = SafeNormalize(horizVec, kUnitXAxis, scHorizArm);
			F32 horizArm = scHorizArm;
			if (Abs(vertArm) > g_powerRagdollHitVertMax)
			{
				vertArm *= g_powerRagdollHitVertMax / Abs(vertArm);
				armLs.SetZ(vertArm);
			}
			if (horizArm < g_powerRagdollHitHorizMin)
			{
				horizArm = g_powerRagdollHitHorizMin;
				horizVec *= horizArm;
				armLs.SetX(horizVec.X());
				armLs.SetY(horizVec.Y());
			}

			// Put back
			pnt = com + Rotate(bodyLoc.GetRotation(), armLs);
		}
	}

	pRagdollBody->ApplyPointImpulse(pnt, dir * impulse, g_powerRagdollHitImpulseTime);

	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_debugPowerRagdoll))
	{
		g_prim.Draw(DebugLine(pnt, pnt - dir, kColorGreen), Seconds(0.3f));
		g_prim.Draw(DebugSphere(pnt, 0.01f, kColorGreen), Seconds(0.3f));
		HavokMarkForReadJanitor jj;
		HavokDebugDrawRigidBody(pRagdollBody->GetHavokBodyId(),
								pRagdollBody->GetLocatorCm(),
								kColorRed,
								CollisionDebugDrawConfig(),
								Seconds(0.3f));
		// g_ndConfig.m_pDMenuMgr->SetProgPauseDebugImmediate();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::TakeHit(const HitDescription* pHitDesc)
{
	ProcessMeleeAction* pRemoveMeleeAction = nullptr;
	Npc* pRemoveMeleeNpc = nullptr;

	bool result = TakeHitInternal(pHitDesc, pRemoveMeleeAction, pRemoveMeleeNpc);

	if (pRemoveMeleeNpc && pRemoveMeleeAction)
	{
		// This can cause script callbacks which request Death reactions, which cause a deadlock, so do it last, outside of the internal lock scope.
		pRemoveMeleeAction->RemoveCharacter(pRemoveMeleeNpc, kCharacterSyncAbort, SID("fullbody hit reaction"));
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::TakeHitInternal(const HitDescription* pHitDesc,
									  ProcessMeleeAction*& pRemoveMeleeAction,
									  Npc*& pRemoveMeleePartner)
{
	AtomicLockJanitorWrite jj(&m_lock, FILE_LINE_FUNC);

	m_lastHitReactionMeleeAnim = INVALID_STRING_ID_64;

	if (!pHitDesc)
		return false;

	// when we are blocking, don't take hits
	if (m_lastReactionWasBlock && !m_fullBodyAnimAction.IsDone())
		return false;

	Npc* pNpc = (Npc*)GetCharacter();
	const Clock* pClock			= pNpc->GetClock();
	const TimeFrame currentTime = pNpc->GetCurTime();

	if (pHitDesc->m_npcState & DC::kHitReactionStateMaskArmorDestroyed)
	{
		m_lastHitAdditiveWindowSec = -1.0f;
		m_lastHitAnimTime = TimeFrameNegInfinity();
	}

	const bool inLastHitWindow = m_pLastSelectedHitEntry && (m_pLastSelectedHitEntry->m_hitTimeout > 0.0f)
								 && !pClock->TimePassed(Seconds(m_pLastSelectedHitEntry->m_hitTimeout),
														m_lastHitAnimTime);

	const bool lastHitWasAdditive = m_pLastSelectedHitEntry
									&& (m_pLastSelectedHitEntry->m_animType == DC::kHitReactionAnimTypeAdditive
										|| m_pLastSelectedHitEntry->m_animType
											   == DC::kHitReactionAnimTypePowerRagdollAndAdditive);

	AnimControl* pAnimControl = pNpc->GetAnimControl();
	const NdAnimControllerConfig* pConfig = pNpc->GetAnimControllerConfig();

	const float hitDirLen = Length(pHitDesc->m_directionWs);
	if (hitDirLen < 0.001f)
	{
		// Not hit from any particular direction... what to do?
		RememberHit(pHitDesc);

		if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_visualizeHitReactions))
		{
			MsgAnimErr("Not hit from any particular direction... what to do?\n");
			VisualizeHitReaction(pHitDesc, nullptr);
		}

		return false;
	}

	const StringId64 scriptSetId = pConfig->m_scriptOverrideHitReaction.m_hitReactionSetId;

	const StringId64 setId = (scriptSetId != INVALID_STRING_ID_64) ? scriptSetId
																   : pConfig->m_hitReaction.m_hitReactionSetId;
	StringId64 bucketId = GetCurrentActiveBucket();

	if (bucketId == SID("default-high") || bucketId == SID("default-med") || bucketId == SID("default-low"))
	{
		// Default buckets don't shift priority
		bucketId = INVALID_STRING_ID_64;
	}

	const StringId64 bucketPriorityIds[] = { bucketId, SID("default-high"), SID("default-med"), SID("default-low") };
	const U32F kNumBuckets = ARRAY_COUNT(bucketPriorityIds);

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	U32F numPotentialAnims = 0;

	U32F animationIndex = kInvalidReactionIndex;

	CoverActionPack* pCoverApToTryFor = nullptr;
	AiPost post;

	if (pNpc->IsBuddyNpc())
	{
		pCoverApToTryFor = GetBuddyCoverDiveAp(pNpc, post);
	}
	else
	{
		if (pNpc->GetIdealPost(DC::kAiPostSelectorPanic, post) && post.IsValid())
		{
			pCoverApToTryFor = (CoverActionPack*)post.GetActionPack();

			if (pCoverApToTryFor && !pCoverApToTryFor->IsAvailableFor(pNpc))
			{
				pCoverApToTryFor = nullptr;
			}
		}
	}

#ifdef KOMAR
	if (pNpc->IsBuddyNpc())
	{
		if (pCoverApToTryFor)
		{
			g_prim.Draw(DebugSphere(pCoverApToTryFor->GetDefensivePosWs(), 1.0f, kColorOrange), Seconds(4.0f));
			ScreenSpaceTextPrinter printer(pCoverApToTryFor->GetDefensivePosWs() + Vector(0.0f, 1.5f, 0.0f));
			printer.SetDuration(Seconds(4.0f));
			printer.PrintText(kColorOrange, "HIT REACT TOWARD");
		}
	}
#endif

	PotentialHitAnim* pPotentialAnims = nullptr;
	for (int iBucketId = 0; iBucketId < kNumBuckets; iBucketId++)
	{
		bucketId = bucketPriorityIds[iBucketId];
		const DC::HitReactionEntryList* pHitList = GetHitAnimationList(setId, bucketId);

		if ((!pHitList || (pHitList->m_count <= 0)))
		{
			continue;
		}

		const U32F maxAnims = pHitList->m_count;
		if (0 == maxAnims)
			return false;

		NDI_DELETE[] pPotentialAnims;
		pPotentialAnims = NDI_NEW PotentialHitAnim[maxAnims];

		const AnimTypeFilter animTypeFilter = GetAnimTypeFilter(pHitDesc);
		const float randomChanceVal			= GetRandomChanceValue();

		numPotentialAnims = BuildPotentialHitAnimList(pHitList,
													  pHitDesc,
													  animTypeFilter,
													  randomChanceVal,
													  pPotentialAnims,
													  pHitList->m_count,
													  inLastHitWindow,
													  lastHitWasAdditive);

		animationIndex = SelectHitReactionAnim(pPotentialAnims, numPotentialAnims, pCoverApToTryFor);

		if (animationIndex != kInvalidReactionIndex && animationIndex < numPotentialAnims)
		{
			break;
		}
	}

	RememberHit(pHitDesc);

	if (!m_partialOverride)
	{
		if (animationIndex == kInvalidReactionIndex || animationIndex >= numPotentialAnims || !pPotentialAnims)
		{
			if (FALSE_IN_FINAL_BUILD(!inLastHitWindow && g_aiGameOptions.m_hitReaction.m_visualizeHitReactions))
			{
				MsgAnimErr("Could Not Find Valid Anim in any Hit List\n");
				VisualizeHitReaction(pHitDesc, nullptr);
			}

			return false;
		}
	}

	const DC::HitReactionEntry* pHit = nullptr;
	BoundFrame tempBf(kIdentity);

	float blendTime	   = 0.1f;
	BoundFrame& bf	   = tempBf;
	float playbackRate = 1.0f;
	float startPhase   = 0.0f;

	const bool usePartialOverride = m_partialOverride
									&& (pHitDesc->m_sourceType != DC::kHitReactionSourceTypeThrownObject)
									&& (pHitDesc->m_sourceType != DC::kHitReactionSourceTypeBuddyThrownObject);

	{
		if (!usePartialOverride && ((animationIndex == kInvalidReactionIndex) || (animationIndex >= numPotentialAnims)))
		{
			MsgAnimErr("No matching hit reaction found for [bucket: %s] [dir: %s] [joint: %s] [attack: %s] [state: %s] [damage: %d]\n",
					   DevKitOnly_StringIdToStringOrHex(GetCurrentActiveBucket()),
					   GetDirectionMaskName(GetHitDirection(*pHitDesc)),
					   DevKitOnly_StringIdToStringOrHex(GetHitJointId(*pHitDesc)),
					   GetAttackTypeName(pHitDesc->m_sourceType),
					   GetNpcStateName(pHitDesc->m_npcState),
					   pHitDesc->m_damage);

			MsgAnimErr(" (%.4f) Aborting hit reaction!\n", Limit01(pHitDesc->m_normalizedHealth));
			// 		AI_ASSERT(animationIndex != kInvalidReactionIndex);

#if !FINAL_BUILD
			MailMissingHitReactionInfo(pHitDesc, "HIT", setId);

			if (g_aiGameOptions.m_hitReaction.m_visualizeHitReactions)
			{
				VisualizeHitReaction(pHitDesc, nullptr);
			}
#endif // !FINAL_BUILD

			return false;
		}

		if (animationIndex != kInvalidReactionIndex)
		{
			blendTime = pPotentialAnims[animationIndex].m_blendTime;
			bf		  = pPotentialAnims[animationIndex].m_apRef;

			pHit = pPotentialAnims[animationIndex].m_pDcEntry;
			if (g_aiGameOptions.m_hitReaction.m_hitReactionMode != AiGameOptions::kHitReactionModeLast)
			{
				m_pLastSelectedHitEntry = pHit;
			}

			const DC::AiRangeval* pRateRange = m_pLastSelectedHitEntry ? m_pLastSelectedHitEntry->m_playbackRateRange
																	   : nullptr;
			const DC::AiRangeval* pPhaseTweak = m_pLastSelectedHitEntry ? m_pLastSelectedHitEntry->m_startPhaseTweak
																		: nullptr;

			playbackRate = pRateRange ? AI::RandomRange(pRateRange) : 1.0f;
			const float baseStartPhase = pPotentialAnims[animationIndex].m_startPhase;
			const float minTweak	   = pPhaseTweak ? pPhaseTweak->m_val0 : 0.0f;
			const float maxTweak	   = pPhaseTweak ? pPhaseTweak->m_val1 : 0.0f;
			startPhase = RandomFloatRange(Limit01(baseStartPhase + minTweak), Limit01(baseStartPhase + maxTweak));

			if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_visualizeHitReactions))
			{
				if (g_aiGameOptions.m_hitReaction.m_hitReactionLogMode & AiGameOptions::kLogModeVisualize)
				{
					const Locator alignWs		  = pNpc->GetLocator();
					const JointCache* pJointCache = pAnimControl->GetJointCache();
					PoseMatchInfo matchInfo;
					const I32F rsIndex = pNpc->FindJointIndex(SID("r_shoulder"));
					const I32F lsIndex = pNpc->FindJointIndex(SID("l_shoulder"));
					if (rsIndex >= 0 && lsIndex >= 0)
					{
						const Locator rootWs	= pJointCache->GetJointLocatorWs(0);
						const Locator leftShWs	= pJointCache->GetJointLocatorWs(lsIndex);
						const Locator rightShWs = pJointCache->GetJointLocatorWs(rsIndex);

						const Locator rootOs	= alignWs.UntransformLocator(rootWs);
						const Locator leftShOs	= alignWs.UntransformLocator(leftShWs);
						const Locator rightShOs = alignWs.UntransformLocator(rightShWs);

						const BoundFrame& frame = pNpc->GetBoundFrame();

						g_prim.Draw(DebugCoordAxesLabeled(rootWs, "root"), Seconds(3.0f), &frame);
						g_prim.Draw(DebugCoordAxesLabeled(leftShWs, "leftSh"), Seconds(3.0f), &frame);
						g_prim.Draw(DebugCoordAxesLabeled(rightShWs, "rightSh"), Seconds(3.0f), &frame);

						GetDistanceErrorForAnim(pNpc->GetSkeletonId(),
												pPotentialAnims[animationIndex].m_pAnim,
												pPotentialAnims[animationIndex].m_startPhase,
												rootOs,
												leftShOs,
												rightShOs,
												true);
					}
				}
				VisualizeHitReaction(pHitDesc, &pPotentialAnims[animationIndex], startPhase, playbackRate);
			}
		}
	}

	if (m_pLastSelectedHitEntry || usePartialOverride)
	{
		if (m_pLastSelectedHitEntry)
		{
			MsgAnimWarn("Playing %s in states (%s) (%s) (%s) with %.1f%% health\n",
						DevKitOnly_StringIdToString(m_pLastSelectedHitEntry->m_animId),
						GetDirectionMaskName(GetHitDirection(*pHitDesc)),
						GetAttackTypeName(pHitDesc->m_sourceType),
						GetNpcStateName(pHitDesc->m_npcState),
						pNpc->GetHealthSystem()->GetHealthPercentage() * 100.0f);

			if (m_pLastSelectedHitEntry->m_voxId)
			{
				RequestVoxArgs voxArgs;
				voxArgs.m_voxId = m_pLastSelectedHitEntry->m_voxId;
				voxArgs.m_eBroadcastMode = DC::kVoxBroadcastModeLocal;

				pNpc->GetVoxController().RequestVox(voxArgs);
			}
		}

		bool useDc = m_pLastSelectedHitEntry;

		StringId64 partialOverride = m_partialOverride;
		if (m_partialOverride
			&& !(pHitDesc->m_sourceType & (DC::kHitReactionSourceTypeFireStrong | DC::kHitReactionSourceTypeFireWeak)))
		{
			if (!m_pLastSelectedHitEntry || m_pLastSelectedHitEntry->m_animType != DC::kHitReactionAnimTypeFullBody)
			{
				/*if (g_meleeOptions.m_powerRagdollHits)
				{
					PowerRagdollHit(pNpc, pHitDesc);
				}*/
				const DC::HitPartialArray* pArray = ScriptManager::Lookup<DC::HitPartialArray>(m_partialOverride,
																							   nullptr);
				if (pArray)
				{
					if (g_meleeOptions.m_forcePartialOverrideIndex >= 0
						&& g_meleeOptions.m_forcePartialOverrideIndex < pArray->m_count)
					{
						partialOverride = pArray->m_array[g_meleeOptions.m_forcePartialOverrideIndex].m_animId;
					}
					else
					{
						// const DC::HitReactionMatchRating *pSetting = ScriptManager::Lookup<DC::HitReactionMatchRating>(SID("*npc-hit-reaction-match-ratings*"))
						float bestRating = -FLT_MAX;
						for (I32 i = 0; i < pArray->m_count; i++)
						{
							if (pArray->m_array[i].m_validFunc)
							{
								ProcessMeleeAction* pAction = pNpc->GetCurrentMeleeAction();

								if (pAction)
								{
									bool valid = pAction->RunHitPartialValidFunc(pHitDesc->m_hSourceGameObj.ToProcess(),
																				 pNpc,
																				 pArray->m_array[i].m_validFunc);
									if (!valid)
										continue;
								}
							}

							float rating = RateAnimationForMeleeImpact(pHitDesc->m_impactPointWs,
																	   pHitDesc->m_directionWs,
																	   SID("*npc-hit-partial-match-ratings*"),
																	   pNpc,
																	   pArray->m_array[i].m_animId);
							if (rating > bestRating)
							{
								bestRating		= rating;
								partialOverride = pArray->m_array[i].m_animId;
							}
						}
					}
				}

				if (!g_meleeOptions.m_disableFullBodyPartials)
				{
					F32 partialStartPhase = 0.0f;
					if (const ArtItemAnim* pAnim = pAnimControl->GetAnimTable().LookupAnim(partialOverride).ToArtItem())
					{
						partialStartPhase = m_partialOverrideFrame * pAnim->m_pClipData->m_phasePerFrame;
					}
					else
					{
						MsgErr("Hit Partial Anim Not Loaded! '%s'\n", DevKitOnly_StringIdToString(partialOverride));
					}
					DC::AnimNpcTopInfo* pNpcInfo = pAnimControl->TopInfo<DC::AnimNpcTopInfo>();
					pNpcInfo->m_currentPartialMeleeAnim = partialOverride;

					FadeToStateParams params;
					params.m_stateStartPhase = partialStartPhase;
					params.m_blendType		 = DC::kAnimCurveTypeUniformS;
					params.m_customFeatherBlendTableIndex = m_featherBlendIndex;

					m_partialOverrideAnimAction.FadeToState(pAnimControl,
															SID("s_npc-hit-reaction-partial"),
															params,
															AnimAction::kFinishOnAnimEndEarlyByBlend);

					AnimStateLayer* pLayer = (AnimStateLayer*)pNpc->GetAnimControl()
												 ->GetLayerById(m_partialOverrideAnimAction.GetLayerId());
					if (pLayer)
					{
						pLayer->SetCurrentFade(0.0f);
						pLayer->Fade(1.0f, m_partOverrideBlendInTime);
						// pLayer->SetFeatherBlendIndex(m_featherBlendIndex);
						m_partialOverrideAnimAction.SetBlendOutTime(m_partOverrideBlendOutTime);

						if (g_meleeOptions.m_hitPartialLimbIk)
						{
							NdSubsystem* pBlendIk = (NdSubsystem*)pNpc->GetSubsystemMgr()
														->FindSubsystem(SID("HitReactionBlendIk"));
							if (!pBlendIk)
							{
								// This will kill itself after the blend
								SubsystemSpawnInfo info(SID("HitReactionBlendIk"), pNpc);
								info.m_pUserData = &m_partOverrideBlendInTime;
								NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap, info, FILE_LINE_FUNC);
							}
						}

						if (ProcessMeleeAction* pAction = pNpc->GetCurrentMeleeAction())
						{
							pAction->DisableHitPartials();
						}
					}

					if (!m_partialOverrideAnimAction.Failed())
						useDc = false;
				}
			}

			if (!m_pLastSelectedHitEntry)
				return false;
		}

		if (useDc)
		{
			ProcessMeleeAction* pAction = pNpc->GetCurrentMeleeAction();
			if (pAction)
			{
				switch (m_pLastSelectedHitEntry->m_animType)
				{
				case DC::kHitReactionAnimTypeFullBody:
					if (pAction->NoFullBodyHitReaction())
						return false;
					pRemoveMeleeAction	= pAction;
					pRemoveMeleePartner = pNpc;
					break;
				default:
					if (pAction->NoPartialHitReaction())
						return false;
					break;
				}
			}

			switch (m_pLastSelectedHitEntry->m_animType)
			{
			case DC::kHitReactionAnimTypeFullBody:
				{
					m_stunAnimEndTime = TimeFramePosInfinity();

					m_pLastFullBodyHitEntry = pHit;
					m_lastFullBodyHitReactionState = pHitDesc->m_npcState;
					m_lastHitReactionBucket		   = bucketId;

					if (!pHit)
					{
						MsgAnimErr("%s failed to resolve valid hit reaction!\n", pNpc->GetName());
						return false;
					}

					m_lastHitAdditiveWindowSec = pHit->m_additiveWindowSec;
					m_lastFullBodyHitAnimTime  = pNpc->GetCurTime();

					if (m_hitDirOverrideValid)
					{
						m_hitDirOverrideValid = false;
					}

					const Locator& currentLocPs = pNpc->GetLocatorPs();
					const NavControl* pNavCon	= GetNavControl();

					if ((pNpc->GetCurrentDemeanor() != kDemeanorAggressive)
						&& IsEnemy(pNpc->GetFactionId(), FactionIdPlayer()))
					{
						pNpc->ForceDemeanor(kDemeanorAggressive, AI_LOG);
					}

					m_lastFullBodyFadeOutPhase = m_pLastFullBodyHitEntry->m_fadeOutPhase;
					if (m_pLastFullBodyHitEntry->m_fadeOutFrame >= 0.0f)
					{
						m_lastFullBodyFadeOutPhase = Min(1.0f,
														 m_pLastFullBodyHitEntry->m_fadeOutFrame / 30.0f
															 / GetDuration(pPotentialAnims[animationIndex].m_pAnim));
					}

					PlayAnimParams params;
					params.m_pAnimAction = &m_fullBodyAnimAction;
					params.m_animId		 = m_pLastSelectedHitEntry->m_animId;
					params.m_startPhase	 = startPhase;
					params.m_exitPhase	 = Max(m_pLastFullBodyHitEntry->m_exitPhase, m_lastFullBodyFadeOutPhase);

					params.m_frame	   = bf;
					params.m_mirror	   = pPotentialAnims[animationIndex].m_mirror;
					params.m_blendTime = blendTime;
					params.m_motionBlendTime = blendTime;

					if (m_pLastFullBodyHitEntry->m_motionBlendTimeOverride >= 0.0f)
					{
						params.m_motionBlendTime = m_pLastFullBodyHitEntry->m_motionBlendTimeOverride;
					}

					params.m_playbackRate = playbackRate;
					params.m_gestureId	  = pHitDesc->m_tryToFireBack ? pHit->m_fireBackGesture : INVALID_STRING_ID_64;
					params.m_handoffType  = HandoffType::kNone;

					const SkeletonId skelId = pNpc->GetSkeletonId();
					const Locator framePs	= bf.GetLocatorPs();
					if (!FindAlignFromApReference(skelId,
												  pPotentialAnims[animationIndex].m_pAnim,
												  params.m_exitPhase,
												  framePs,
												  SID("apReference"),
												  &m_fullBodyExitAlignPs,
												  params.m_mirror))
					{
						m_fullBodyExitAlignPs = framePs;
					}

					if (m_pLastSelectedHitEntry->m_recoveryList)
					{
					}
					else
					{
						switch (m_pLastSelectedHitEntry->m_exitMode)
						{
						case DC::kHitReactionExitModeMoving:
							params.m_handoffType = HandoffType::kMoving;
							params.m_dtGroupId	 = SID("hit-exit^move");
							break;

						case DC::kHitReactionExitModeCover:
							if (const CoverActionPack* pCoverAp = CoverActionPack::FromActionPack(pNpc->GetEnteredActionPack()))
							{
								m_hRecoveryAp = pCoverAp;
							}
							else
							{
								params.m_handoffType = HandoffType::kIdle;
								params.m_dtGroupId	 = SID("hit-exit^idle");
							}
							break;

						case DC::kHitReactionExitModePerch:
							if (const PerchActionPack* pPerchAp = PerchActionPack::FromActionPack(pNpc->GetEnteredActionPack()))
							{
								m_hRecoveryAp = pPerchAp;
							}
							else
							{
								params.m_handoffType = HandoffType::kIdle;
								params.m_dtGroupId	 = SID("hit-exit^idle");
							}
							break;

						case DC::kHitReactionExitModeIdle:
							params.m_handoffType = HandoffType::kIdle;
							params.m_dtGroupId	 = SID("hit-exit^idle");
							break;
						}
					}

					if (pNpc->IsAiDisabled())
					{
						params.m_handoffType = HandoffType::kNone;
					}

					if (!AllHitAnimsDisabled(pHitDesc->m_sourceType))
					{
						PlayHitAnim(params);
					}
					else
					{
						return false; // we are not playing this hit reaction
					}

					switch (pHitDesc->m_sourceType)
					{
					case DC::kHitReactionSourceTypeExplosionStrong:
					case DC::kHitReactionSourceTypeExplosionWeak:
					case DC::kHitReactionSourceTypeExplosionFlinch:
						m_lastExplosionReactionAnim = m_pLastSelectedHitEntry->m_animId;
						break;
					}

					if (g_netInfo.IsNetActive())
					{
						m_lastNetReactTime = EngineComponents::GetFrameState()->GetClock(kNetworkClock)->GetCurTime();
						// this was solving the fact that npcs would have delayed hit reactions, but its strange to
						// do this with a net event the time to hit react animation would be about the same
						// otherwise 					SendNetEventNpcHitReaction(pNpc->GetUserId(), setId, bucketId,
						// pPotentialAnims[animationIndex].m_dcIndex, startPhase, playbackRate, bf.GetLocator());
					}

					m_hitRecovery = SelectedRecovery();

					if (m_pLastSelectedHitEntry->m_recoveryList)
					{
						m_exitPhase = 1.1f;
						m_hitRecoverySelected = false;
						TryQueueHitReactionRecovery();
					}
				}
				break;

			case DC::kHitReactionAnimTypePartial:
				{
					m_partialAnimAction.FadeToAnim(pAnimControl, m_pLastSelectedHitEntry->m_animId, 0.0f, 0.1f);
					if (AnimSimpleLayer* pHrParLayer = pAnimControl->GetSimpleLayerById(SID("hit-reaction-partial")))
					{
						pHrParLayer->Fade(1.0f, 0.0f);
					}
				}
				break;

			case DC::kHitReactionAnimTypeAdditive:
				{
					if (!g_meleeOptions.m_disableAdditives)
					{
						m_additiveAnimAction.FadeToAnim(pAnimControl, m_pLastSelectedHitEntry->m_animId, 0.0f, 0.1f);
					}
				}
				break;

			case DC::kHitReactionAnimTypePowerRagdollAndAdditive:
				{
					if (!g_meleeOptions.m_disableAdditives)
					{
						m_additiveAnimAction.FadeToAnim(pAnimControl, m_pLastSelectedHitEntry->m_animId, 0.0f, 0.1f);
					}

					PowerRagdollHit(pNpc,
									pHitDesc,
									m_pLastSelectedHitEntry->m_ragdollSettings,
									m_pLastSelectedHitEntry->m_hitImpulse);
				}
				break;

			case DC::kHitReactionAnimTypePowerRagdoll:
				{
					PowerRagdollHit(pNpc,
									pHitDesc,
									m_pLastSelectedHitEntry->m_ragdollSettings,
									m_pLastSelectedHitEntry->m_hitImpulse);
				}
				break;
			}

			if (pHitDesc->m_sourceType == DC::kHitReactionSourceTypeMelee)
			{
				m_lastHitReactionMeleeAnim = m_pLastSelectedHitEntry->m_animId;
			}

			if (m_pLastSelectedHitEntry->m_flags & DC::kHitReactionFlagsResetShotWindow)
			{
				ResetShotWindow();
			}

			if (m_pLastSelectedHitEntry->m_animType == DC::kHitReactionAnimTypeFullBody)
			{
				if (pHitDesc->m_sourceType != DC::kHitReactionSourceTypeMeleeDanger)
				{
					m_interruptSkills = true;
				}

				m_interruptNavigation = true;

				m_lastFullBodySourceType = pHitDesc->m_sourceType;
				m_lastFullBodyFromPlayer = pHitDesc->m_hSourceGameObj.HandleValid() && pHitDesc->m_hSourceGameObj.IsKindOf(g_type_PlayerBase);
				m_lastHitAnimTime		 = pNpc->GetCurTime();

				if (pHitDesc->m_npcState & DC::kHitReactionStateMaskIgnited)
					pNpc->SignalIgnitionReaction();
				return true;
			}
		}

		m_lastHitAnimTime = pNpc->GetCurTime();
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::Die(const HitDescription& hitDesc, bool fromScript)
{
	StringId64 bucketId = GetCurrentActiveBucket();

	if (bucketId == SID("default-high") || bucketId == SID("default-med") || bucketId == SID("default-low"))
	{
		// Default buckets don't shift priority
		bucketId = INVALID_STRING_ID_64;
	}

	// if we get shot twice on the same frame, we need to make sure nothing stomps our death animation (such as a pending exit anim action)
	CancelAnimAction();

	NavCharacter* pChar = GetCharacter();
	g_ssMgr.BroadcastEvent(SID("npc-went-missing"), pChar->GetUserId());

	if (DieWithDeathHint())
		return;

	if (DieOnVehicleAsDriver())
		return;

	if (DieOnBoatHack(hitDesc))
		return;

	AnimControl* pAnimControl = pChar->GetAnimControl();
	const NdAnimControllerConfig* pConfig = pChar->GetAnimControllerConfig();

	const StringId64 scriptSetId = pConfig->m_scriptOverrideHitReaction.m_deathReactionSetId;
	const StringId64 setId		 = (scriptSetId != INVALID_STRING_ID_64) ? scriptSetId
																   : pConfig->m_hitReaction.m_deathReactionSetId;

	const StringId64 bucketPriorityIds[] = { bucketId, SID("default-high"), SID("default-med"), SID("default-low") };
	const U32F kNumBuckets = ARRAY_COUNT(bucketPriorityIds);

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);
	PotentialHitAnim* pPotentialAnims = nullptr;
	const DC::HitReactionEntryList* pDeathList = nullptr;

	U32F numPotentialAnims = 0;
	U32F animationIndex	   = kInvalidReactionIndex;
	U32F maxDeathAnims	   = 0;

	const float randomChanceVal = GetRandomChanceValue();

	for (int iBucket = 0; iBucket < kNumBuckets; iBucket++)
	{
		pDeathList = GetDeathAnimationList(setId, bucketPriorityIds[iBucket]);

		maxDeathAnims = pDeathList ? pDeathList->m_count : 0;

		if (maxDeathAnims == 0)
			continue;

		// Scoped temp will clean up the orphans
		pPotentialAnims = NDI_NEW PotentialHitAnim[maxDeathAnims];

		numPotentialAnims = BuildPotentialHitAnimList(pDeathList,
													  &hitDesc,
													  AnimTypeFilter::kNone,
													  randomChanceVal,
													  pPotentialAnims,
													  pDeathList->m_count,
													  false,
													  false);
		animationIndex	  = SelectHitReactionAnim(pPotentialAnims, numPotentialAnims, nullptr);

		if (animationIndex != kInvalidReactionIndex && animationIndex < numPotentialAnims)
		{
			break;
		}
	}

	FadeOutNonDeathLayers();

	if ((animationIndex == kInvalidReactionIndex) || (animationIndex >= numPotentialAnims))
	{
		// npc-die script func only uses this function to TRY dying with death hint or vehicle death anim
		// if we reach this point from npc-die, it's not really an error
		if (fromScript)
			return;

#if !FINAL_BUILD
		if (g_aiGameOptions.m_hitReaction.m_visualizeHitReactions)
		{
			VisualizeHitReaction(&hitDesc, nullptr);
		}

		if ((hitDesc.m_npcState & DC::kHitReactionStateMaskAdditiveOnly) == 0)
		{
			MailMissingHitReactionInfo(&hitDesc, "DEATH", setId);
		}
#endif // !FINAL_BUILD

		if (FALSE_IN_FINAL_BUILD(true))
		{
			StringBuilder<256> errMsg;

			errMsg.format("No matching death reaction found!\n  [bucket: %s] [dir: %s]\n  [joint: %s] [attack: %s]\n  [state: %s] [damage: %d]\n  [set: %s]\n",
						  DevKitOnly_StringIdToStringOrHex(GetCurrentActiveBucket()),
						  GetDirectionMaskName(GetHitDirection(hitDesc)),
						  DevKitOnly_StringIdToStringOrHex(GetHitJointId(hitDesc)),
						  GetAttackTypeName(hitDesc.m_sourceType),
						  GetNpcStateName(hitDesc.m_npcState),
						  hitDesc.m_damage,
						  DevKitOnly_StringIdToString(setId));

			MsgAnimErr("[%s] %s", pChar->GetName(), errMsg.c_str());

			AiLogAnim(pChar, errMsg.c_str());

			g_prim.Draw(DebugString(pChar->GetTranslation() + kUnitYAxis, errMsg.c_str(), kColorRed, 0.75f), Seconds(15.0f));
		}

		if (IsBusy())
		{
			pChar->AllowRagdoll();
			pChar->PhysicalizeRagdoll();
			pChar->FadeOutRagdollPower(-1.0f, "Died with no death animation while playing full body hit reaction");
			MsgAnim("Died with no death animation while playing full body hit reaction, forcing ragdoll\n");
		}

		if (FALSE_IN_FINAL_BUILD((hitDesc.m_npcState & DC::kHitReactionStateMaskAdditiveOnly) == 0))
		{
			PlayAnimParams idleZenParams;
			idleZenParams.m_pAnimAction = &m_fullBodyAnimAction;
			idleZenParams.m_animId		= SID("idle-zen");
			idleZenParams.m_startPhase	= 0.0f;
			idleZenParams.m_exitPhase	= -1.0f;
			idleZenParams.m_frame		= pChar->GetBoundFrame();

			PlayDeathAnim(idleZenParams);
		}

		return;
	}

	AI_ASSERT(pPotentialAnims[animationIndex].m_pDcEntry);

	const PotentialHitAnim& selectedPotential = pPotentialAnims[animationIndex];
	const DC::HitReactionEntry& selectedAnim  = *selectedPotential.m_pDcEntry;

	m_pLastFullBodyHitEntry = nullptr;
	m_lastFullBodyHitReactionState = 0;
	m_pLastSelectedHitEntry = &selectedAnim;

	if (selectedAnim.m_animType == DC::kHitReactionAnimTypeFullBody)
	{
		m_pLastFullBodyHitEntry		   = &selectedAnim;
		m_lastFullBodyHitReactionState = hitDesc.m_npcState;
	}

	const DC::AiRangeval* pRateRange = m_pLastSelectedHitEntry ? m_pLastSelectedHitEntry->m_playbackRateRange : nullptr;
	const DC::AiRangeval* pPhaseTweak = m_pLastSelectedHitEntry ? m_pLastSelectedHitEntry->m_startPhaseTweak : nullptr;

	const float playbackRate   = pRateRange ? AI::RandomRange(pRateRange) : 1.0f;
	const float baseStartPhase = pPotentialAnims[animationIndex].m_startPhase;
	const float minTweak	   = pPhaseTweak ? pPhaseTweak->m_val0 : 0.0f;
	const float maxTweak	   = pPhaseTweak ? pPhaseTweak->m_val1 : 0.0f;
	const float startPhase = RandomFloatRange(Limit01(baseStartPhase + minTweak), Limit01(baseStartPhase + maxTweak));

	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_visualizeHitReactions))
	{
		VisualizeHitReaction(&hitDesc, &selectedPotential, startPhase, playbackRate);
	}

	const bool mirror = (selectedAnim.m_flags & DC::kHitReactionFlagsMirror);

	MsgAnimWarn("Playing %s%s in states (%s) (%s)\n",
				DevKitOnly_StringIdToString(selectedAnim.m_animId),
				mirror ? " (mirrored)" : "",
				GetAttackTypeName(hitDesc.m_sourceType),
				GetNpcStateName(hitDesc.m_npcState));

	const float exitPhase = m_pLastFullBodyHitEntry ? m_pLastFullBodyHitEntry->m_exitPhase : 1.0f;

	const StringId64 fireBackGestureId = (hitDesc.m_tryToFireBack && selectedPotential.m_pDcEntry)
											 ? selectedPotential.m_pDcEntry->m_fireBackGesture
											 : INVALID_STRING_ID_64;

	PlayAnimParams params;
	params.m_pAnimAction  = &m_fullBodyAnimAction;
	params.m_animId		  = selectedAnim.m_animId;
	params.m_startPhase	  = startPhase;
	params.m_exitPhase	  = exitPhase;
	params.m_frame		  = selectedPotential.m_apRef;
	params.m_mirror		  = mirror;
	params.m_playbackRate = playbackRate;
	params.m_gestureId	  = fireBackGestureId;
	params.m_blendTime	  = selectedPotential.m_blendTime;
	params.m_motionBlendTime = selectedPotential.m_blendTime;

	AiLogAnim(pChar,
			  "Playing Death Animation '%s' [%0.3f -> %0.3f] %s\n",
			  DevKitOnly_StringIdToString(selectedAnim.m_animId),
			  startPhase,
			  exitPhase,
			  mirror ? "(Flipped)" : "");

	if (m_pLastSelectedHitEntry && m_pLastSelectedHitEntry->m_motionBlendTimeOverride >= 0.0f)
	{
		params.m_motionBlendTime = m_pLastSelectedHitEntry->m_motionBlendTimeOverride;
	}

	if (pChar->GetEnteredVehicle().HandleValid())
	{
		// if we get here, then NPC is a vehicle passenger
		params.m_customApRefId = SID("apReference-railRef");
		pChar->SetDeathAnimApChannel(params.m_customApRefId);
	}

	if (selectedAnim.m_lookAtSource || selectedAnim.m_aimAtSource)
	{
		Npc* pNpc = Npc::FromProcess(pChar);

		AiLookAimRequest request;
		if (const NdGameObject* pSourceObj = hitDesc.m_hSourceGameObj.ToProcess())
		{
			LookAimObjectParams reqParams(hitDesc.m_hSourceGameObj);
			request = AiLookAimRequest(SID("LookAimObject"), &reqParams);
		}
		else
		{
			LookAimPointWsParams reqParams(hitDesc.m_sourcePointWs);
			request = AiLookAimRequest(SID("LookAimPointWs"), &reqParams);
		}

		if (selectedAnim.m_lookAtSource && selectedAnim.m_aimAtSource)
		{
			pNpc->SetLookAimMode(kLookAimPrioritySystem, request, FILE_LINE_FUNC);
		}
		else if (selectedAnim.m_lookAtSource)
		{
			pNpc->GetLookAim().SetLookMode(kLookAimPrioritySystem, request, FILE_LINE_FUNC);
		}
		else
		{
			pNpc->GetLookAim().SetAimMode(kLookAimPrioritySystem, request, FILE_LINE_FUNC);
		}
	}

	PlayDeathAnim(params);

	if (selectedPotential.m_pDcEntry && selectedPotential.m_pDcEntry->m_fireBackGesture != INVALID_STRING_ID_64)
	{
		pChar->HoldOnToWeaponDuringDeath();
	}

	if (m_pLastSelectedHitEntry)
	{
		if (CompositeRagdollController* pRagdollCtrl = GetCharacter()->GetRagdollController())
		{
			// If we want to play anim on early power off, set the keep motors flag
			pRagdollCtrl->SetKeepMotors(m_pLastFullBodyHitEntry->m_earlyRagdollPowerOffSettings != INVALID_STRING_ID_64);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::DieSpecial(StringId64 deathAnim,
								 const BoundFrame* pApOverride /* = nullptr */,
								 StringId64 customApRefId /* = INVALID_STRING_ID_64 */,
								 float fadeInTime /* = -1.0f */)
{
	const float exitPhase = m_pLastFullBodyHitEntry ? m_pLastFullBodyHitEntry->m_exitPhase : 1.0f;
	PlayAnimParams params;
	params.m_pAnimAction   = &m_fullBodyAnimAction;
	params.m_animId		   = deathAnim;
	params.m_exitPhase	   = exitPhase;
	params.m_frame		   = pApOverride ? *pApOverride : GetCharacter()->GetBoundFrame();
	params.m_customApRefId = customApRefId;
	params.m_blendTime	   = fadeInTime;

	PlayDeathAnim(params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsBlocking() const
{
	return m_lastReactionWasBlock && IsHitReactionPlaying();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::FadeOutHitLayers(F32 fadeTime)
{
	AnimControl* pAnimControl = GetCharacter()->GetAnimControl();

	if (AnimLayer* pLayer = pAnimControl->GetLayerById(SID("hit-reaction-additive")))
		pLayer->Fade(0.0f, fadeTime);

	if (AnimLayer* pLayer = pAnimControl->GetLayerById(SID("hit-reaction-partial")))
		pLayer->Fade(0.0f, fadeTime);

	if (AnimLayer* pLayer = pAnimControl->GetLayerById(SID("hit-reaction-partial-override")))
		pLayer->Fade(0.0f, fadeTime);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::FadeOutNonDeathLayers(F32 fadeTime)
{
	AnimControl* pAnimControl = GetCharacter()->GetAnimControl();

	// Disable all other layers
	if (AnimLayer* pLayer = pAnimControl->GetLayerById(SID("weapon-additive")))
		pLayer->Fade(0.0f, fadeTime);

	if (AnimLayer* pLayer = pAnimControl->GetLayerById(SID("weapon-partial")))
		pLayer->Fade(0.0f, fadeTime);

	if (AnimLayer* pLayer = pAnimControl->GetLayerById(SID("hands")))
		pLayer->Fade(0.0f, fadeTime);

	FadeOutHitLayers(fadeTime);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::CancelAnimAction()
{
	AtomicLockJanitorWrite jj(&m_lock, FILE_LINE_FUNC);

	ANIM_ASSERT(TRUE_IN_FINAL_BUILD(!g_aiGameOptions.m_hitReaction.m_assertOnStunFailure) || !IsStunReactionPlaying());

	m_lastReactionWasBlock = false;
	m_interruptNavigation  = false;
	m_interruptSkills	   = false;
	m_ragdollOnAnimEnd	   = false;
	m_lastExplosionReactionAnim = INVALID_STRING_ID_64;

	m_exitAnimAction.Reset();
	m_recoveryAnimAction.Reset();
	m_fullBodyAnimAction.Reset();
	m_partialAnimAction.Reset();
	m_additiveAnimAction.Reset();
	m_partialOverrideAnimAction.Reset();
	m_shoveAnimAction.Reset();
	m_knockDownAnimAction.Reset();
	m_palletReactionAction.Reset();

	m_pLastSelectedHitEntry		   = nullptr;
	m_pLastFullBodyHitEntry		   = nullptr;
	m_lastFullBodyHitReactionState = 0;

	m_exitPhase = -1.0f;

	m_hitDirOverrideValid	= false;
	m_lastHitReactionBucket = INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::RequestEmotionState(float phase, const DC::EmotionRangeArray* pEmotionRange)
{
	if (pEmotionRange == nullptr)
		return;

	Npc* pNpc = Npc::FromProcess(GetCharacter());

	StringId64 desiredEmotion = INVALID_STRING_ID_64;
	for (int i = 0; i < pEmotionRange->m_emotionRangeArrayCount; i++)
	{
		if (pEmotionRange->m_emotionRangeArray[i].m_startPhase < phase)
		{
			desiredEmotion = pEmotionRange->m_emotionRangeArray[i].m_emotion;
		}
	}

	if (desiredEmotion != INVALID_STRING_ID_64)
	{
		pNpc->GetEmotionControl()->SetEmotionalState(desiredEmotion, -1.0f, kEmotionPriorityDeath, 3.0f, 1.0f);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::RequestAnimations()
{
	Npc* pNpc = Npc::FromProcess(GetCharacter());
	AnimControl* pAnimControl = pNpc->GetAnimControl();

	// 	MsgCon("%s\n", DevKitOnly_StringIdToString(GetCurrentActiveBucket()));

	// Check for other npcs that this character might bump into
	if (IsBeingShoved() && m_allowNpcCollision)
	{
		IAiHitController::CheckForNpcCollision(pNpc);
	}

	if (m_pLastFullBodyHitEntry)
	{
		if (!m_hitRecoverySelected)
		{
			TryQueueHitReactionRecovery();
		}

		const AnimStateInstance* pInst	  = m_fullBodyAnimAction.GetTransitionDestInstance(pAnimControl);
		const AnimStateLayer* pBaseLayer  = pAnimControl->GetBaseStateLayer();
		const AnimStateInstance* pCurInst = pBaseLayer->CurrentStateInstance();

		float curPhase = -1.0f;

		const bool stillInHitReactionState = (pInst && (pInst == pCurInst)
											  && ((pInst->GetStateName() == SID("s_hit-reaction-state"))
												  || (pInst->GetStateName() == SID("s_hit-reaction-w-gesture-state"))));

		if (stillInHitReactionState)
		{
			curPhase = pInst->Phase();
		}
		else if (m_fullBodyAnimAction.IsDone())
		{
			AiLogAnim(pNpc,
					  "AiHitController::RequestAnimations my current instance isn't my hit reacion state (%s), aborting fade out logic\n",
					  pCurInst ? DevKitOnly_StringIdToString(pCurInst->GetStateName()) : "<none>");
			m_pLastFullBodyHitEntry		   = nullptr;
			m_lastFullBodyHitReactionState = 0;
			return;
		}
		else if (pInst)
		{
			curPhase = pInst->Phase();
		}

		if (m_pLastSelectedHitEntry)
			RequestEmotionState(curPhase, m_pLastSelectedHitEntry->m_emotions);

		const DC::AiRangeval* pWallDetectionRange = m_pLastFullBodyHitEntry->m_wallDetectionRange;

		if (stillInHitReactionState)
		{
			if (curPhase >= m_lastFullBodyFadeOutPhase)
			{
				if (pBaseLayer->AreTransitionsPending())
				{
					AiLogAnim(pNpc,
							  "AiHitController::RequestAnimations Pending transitions found, aborting fade out logic\n");
					m_pLastFullBodyHitEntry		   = nullptr;
					m_lastFullBodyHitReactionState = 0;
					m_exitAnimAction.Reset();
					return;
				}

				FadeOutAnim();

				m_pLastFullBodyHitEntry		   = nullptr;
				m_lastFullBodyHitReactionState = 0;
			}
			else if (pWallDetectionRange && (curPhase >= pWallDetectionRange->m_val0)
					 && (curPhase <= pWallDetectionRange->m_val1))
			{
				if (pNpc->WasMovementConstrained())
				{
					AiLogAnim(pNpc,
							  "Ending hit reaction '%s' at phase %0.3f because detected running into a wall\n",
							  DevKitOnly_StringIdToString(m_pLastFullBodyHitEntry->m_animId),
							  curPhase);

					FadeToStateParams params;
					params.m_animFadeTime	= 0.5f;
					params.m_motionFadeTime = 0.2f;
					params.m_blendType		= DC::kAnimCurveTypeEaseOut;

					m_exitAnimAction.FadeToState(pAnimControl,
												 SID("s_idle"),
												 params,
												 AnimAction::kFinishOnTransitionTaken);

					m_pLastFullBodyHitEntry		   = nullptr;
					m_lastFullBodyHitReactionState = 0;
					m_fullBodyAnimAction.Reset();
				}
			}
		}
	}
	else if (m_pLastSelectedHitEntry && m_partialAnimAction.IsValid() && !m_partialAnimAction.IsDone())
	{
		float curPhase = m_partialAnimAction.GetAnimPhase(pAnimControl);
		RequestEmotionState(curPhase, m_pLastSelectedHitEntry->m_emotions);
	}
	else if (m_pLastSelectedHitEntry && m_additiveAnimAction.IsValid() && !m_additiveAnimAction.IsDone())
	{
		float curPhase = m_additiveAnimAction.GetAnimPhase(pAnimControl);
		RequestEmotionState(curPhase, m_pLastSelectedHitEntry->m_emotions);
	}

	// TODO (after ship) make this a hit reaction def flag instead
	if (m_fullBodyAnimAction.IsValid() && !m_fullBodyAnimAction.IsDone()
		&& IsHitReactionSourceTypeStun(m_lastFullBodySourceType))
	{
		pNpc->RequestPushPlayerAway();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::FadeOutAnim()
{
	Npc* pNpc = Npc::FromProcess(GetCharacter());
	AnimControl* pAnimControl = pNpc->GetAnimControl();
	AnimationControllers* pAnimControllers = pNpc->GetAnimationControllers();

	if (!m_pLastFullBodyHitEntry)
	{
		return;
	}

	const float fadeTime = m_pLastFullBodyHitEntry->m_fadeOutDuration;

	FadeToStateParams fadeParams;
	fadeParams.m_animFadeTime	= fadeTime;
	fadeParams.m_motionFadeTime = fadeTime;
	fadeParams.m_blendType		= m_pLastFullBodyHitEntry->m_fadeOutCurve;

	if (m_hitRecovery.m_pDcDef)
	{
		PlayAnimParams params;
		params.m_animId		 = m_hitRecovery.m_pDcDef->m_anim;
		params.m_pAnimAction = &m_recoveryAnimAction;

		switch (m_hitRecovery.m_pDcDef->m_type)
		{
		case DC::kHitReactionRecoveryTypeIdle:
			params.m_dtGroupId	 = SID("hit-exit^idle");
			params.m_handoffType = HandoffType::kIdle;
			break;
		case DC::kHitReactionRecoveryTypeMoving:
			params.m_dtGroupId	 = SID("hit-exit^moving");
			params.m_handoffType = HandoffType::kMoving;
			break;
		case DC::kHitReactionRecoveryTypeCover:
			params.m_dtGroupId	 = SID("hit-exit^cover");
			params.m_handoffType = HandoffType::kNone;
			break;
		}

		params.m_frame = m_hitRecovery.m_initialAp;

		CoverActionPack* pCoverAp = m_hitRecovery.m_hCoverAp.ToActionPack<CoverActionPack>();
		if (pCoverAp)
		{
			IAiCoverController* pCoverController = pAnimControllers->GetCoverController();
			if (pCoverController && pCoverAp->Reserve(pNpc, kHitReactionCoverReservationTime))
			{
				ActionPackEntryDef entry;
				entry.m_entryAnimId	  = params.m_animId;
				entry.m_pDcDef		  = nullptr;
				entry.m_apReference	  = m_hitRecovery.m_initialAp;
				entry.m_sbApReference = m_hitRecovery.m_finalAp;
				entry.m_additiveHitReactions = true;

				entry.OverrideSelfBlend(&m_hitRecovery.m_selfBlend);

				if (m_hitRecovery.m_skipPhase > 0.0f)
				{
					entry.m_phase	  = m_hitRecovery.m_pDcDef->m_phaseRange->m_val0;
					entry.m_skipPhase = m_hitRecovery.m_skipPhase;
				}
				else
				{
					entry.m_phase = 0.0f;
				}

				pCoverController->EnterImmediately(pCoverAp, entry);

				pNpc->SwitchActionPack(pCoverAp);

				m_interruptNavigation = false;
			}
			else
			{
				AiLogAnim(pNpc, "Failed to reserve AP for hit reaction recovery, fading to idle\n");
				m_exitAnimAction.FadeToState(pAnimControl,
											 SID("s_idle"),
											 fadeParams,
											 AnimAction::kFinishOnTransitionTaken);
			}
		}
		else if (params.m_handoffType != HandoffType::kNone)
		{
			if (!AllHitAnimsDisabled(DC::kHitReactionSourceTypeMax))
				PlayHitAnim(params);

			if (m_hitRecovery.m_selfBlend.m_phase >= 0.0f)
			{
				const SkeletonId skelId	 = pNpc->GetSkeletonId();
				const ArtItemAnim* pAnim = pAnimControl->LookupAnim(params.m_animId).ToArtItem();
				const Locator alignWs	 = pNpc->GetLocator();
				Locator apRefWs	 = alignWs;
				BoundFrame apRef = pNpc->GetBoundFrame();

				if (FindApReferenceFromAlign(skelId, pAnim, alignWs, &apRefWs, params.m_mirror))
				{
					apRef.SetLocatorWs(apRefWs);
				}

				m_recoveryAnimAction.SetSelfBlendParams(&m_hitRecovery.m_selfBlend, apRefWs, pNpc, 1.0f);
			}
		}
		else
		{
			AiLogAnim(pNpc,
					  "Hit recovery anim '%s' has 'none' handoff type but no cover, fading to idle\n",
					  DevKitOnly_StringIdToString(params.m_animId));

			m_exitAnimAction.FadeToState(pAnimControl, SID("s_idle"), fadeParams, AnimAction::kFinishOnTransitionTaken);
		}
	}
	else
	{
		switch (m_pLastFullBodyHitEntry->m_exitMode)
		{
		case DC::kHitReactionExitModeNone:
			break;

		case DC::kHitReactionExitModeVehicle:
			{
				IAiVehicleController* pVehicleController = pAnimControllers->GetVehicleController();
				if (pVehicleController && pVehicleController->GetCar().HandleValid())
				{
					pVehicleController->Resume();
				}
				else
				{
					m_exitAnimAction.FadeToState(pAnimControl,
												 SID("s_idle"),
												 fadeParams,
												 AnimAction::kFinishOnTransitionTaken);
				}
			}
			break;

		case DC::kHitReactionExitModePerch:
		case DC::kHitReactionExitModeCover:
			{
				bool success = false;
				ActionPack* pRecoverAp = m_hRecoveryAp.ToActionPack();
				ActionPackController* pApController = pAnimControllers->GetControllerForActionPack(pRecoverAp);
				if (pRecoverAp && pApController && pRecoverAp->Reserve(pNpc, kHitReactionCoverReservationTime))
				{
					if (pApController->TeleportInto(pRecoverAp, false, fadeTime, nullptr))
					{
						success = true;
					}
				}

				if (success && pRecoverAp)
				{
					pNpc->SwitchActionPack(pRecoverAp);
					m_exitPhase = -1.0f;
				}
				else
				{
					AiLogAnim(pNpc, "failed to implement exit mode into AP, fading to idle\n");
					m_exitAnimAction.FadeToState(pAnimControl,
												 SID("s_idle"),
												 fadeParams,
												 AnimAction::kFinishOnTransitionTaken);
				}

				m_hRecoveryAp = nullptr;
			}
			break;

		case DC::kHitReactionExitModeMoving:
		case DC::kHitReactionExitModeIdle:
			if (!pNpc->IsAiDisabled())
			{
				const AnimStateLayer* pBaseLayer	  = pAnimControl->GetBaseStateLayer();
				const AnimStateInstance* pCurInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;

				if (pCurInstance)
				{
					NavAnimHandoffDesc desc;
					desc.SetAnimStateInstance(pCurInstance);

					desc.m_motionType	 = m_pLastFullBodyHitEntry->m_exitMode == DC::kHitReactionExitModeMoving ? pNpc->GetRequestedMotionType() : kMotionTypeMax;
					desc.m_steeringPhase = 0.0f;

					pNpc->ConfigureNavigationHandOff(desc, FILE_LINE_FUNC);
				}
				break;
			}
			FALLTHROUGH;
		default:
			m_exitAnimAction.FadeToState(pAnimControl, SID("s_idle"), fadeParams, AnimAction::kFinishOnTransitionTaken);
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::UpdateStatus()
{
	PROFILE(AI, HitCon_UpdateStatus);

	const NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl	 = pNavChar->GetAnimControl();
	const float dt = GetProcessDeltaTime();

	if (IsStunReactionPlaying())
	{
		m_lastStunPlayingTime = pNavChar->GetCurTime();
	}

	if (IsStunReactionPlaying() && !m_fullBodyAnimAction.IsDone() && (pNavChar->GetCurTime() > m_stunAnimEndTime)
		&& (m_fullBodyAnimAction.GetTransitionStatus() != StateChangeRequest::kStatusFlagPending))
	{
		AiLogAnimDetails(pNavChar,
						 "Past stun reaction end time (%f) so ending hit reaction",
						 ToSeconds(m_stunAnimEndTime));
		m_pLastFullBodyHitEntry		   = nullptr;
		m_lastFullBodyHitReactionState = 0;
		m_fullBodyAnimAction.Request(pAnimControl, SID("exit"), AnimAction::kFinishOnTransitionTaken);
	}

	m_exitAnimAction.Update(pAnimControl);
	m_fullBodyAnimAction.Update(pAnimControl);
	m_recoveryAnimAction.Update(pAnimControl);
	m_partialAnimAction.Update(pAnimControl);
	m_partialOverrideAnimAction.Update(pAnimControl);
	m_additiveAnimAction.Update(pAnimControl);
	m_shoveAnimAction.Update(pAnimControl);
	m_knockDownAnimAction.Update(pAnimControl);
	m_palletReactionAction.Update(pAnimControl);

	if (IsBusy() || !m_additiveAnimAction.IsDone() || !m_shoveAnimAction.IsDone())
	{
		m_lastReactTime = GetProcessClock()->GetCurTime();
	}

	const float fadeOutDuration = m_pLastSelectedHitEntry ? Max(m_pLastSelectedHitEntry->m_fadeOutDuration, 0.0f) : 0.1f;

	if (m_partialAnimAction.IsValid() && m_partialAnimAction.IsDone())
	{
		AnimSimpleLayer* hitReactionLayer = pAnimControl->GetSimpleLayerById(SID("hit-reaction-partial"));
		hitReactionLayer->Fade(0.0f, fadeOutDuration);
		m_partialAnimAction.Reset();
	}

	if (m_partialOverrideAnimAction.IsValid() && m_partialOverrideAnimAction.IsDone())
	{
		AnimStateLayer* hitReactionLayer = pAnimControl->GetStateLayerById(SID("hit-reaction-partial-override"));
		hitReactionLayer->Fade(0.0f, m_partOverrideBlendOutTime);
		if (AnimStateInstance* pInst = hitReactionLayer->CurrentStateInstance())
			pInst->SetFeatherBlendTable(m_featherBlendOutIndex);

		m_partialOverrideAnimAction.Reset();
	}

	if (m_additiveAnimAction.IsValid() && m_additiveAnimAction.IsDone())
	{
		AnimSimpleLayer* hitReactionLayer = pAnimControl->GetSimpleLayerById(SID("hit-reaction-additive"));
		hitReactionLayer->Fade(0.0f, fadeOutDuration);
		m_additiveAnimAction.Reset();
	}

	if (m_knockDownAnimAction.IsValid() && m_knockDownAnimAction.IsDone())
	{
		m_knockDownAnimAction.Reset();
	}

	AnimStateInstance* pHitReactionInst = m_fullBodyAnimAction.GetTransitionDestInstance(pAnimControl);

	if (!pHitReactionInst && m_pLastFullBodyHitEntry && m_fullBodyAnimAction.IsDone())
	{
		if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_assertOnStunFailure))
		{
			ANIM_ASSERTF(!IsHitReactionSourceTypeStun(m_pLastFullBodyHitEntry->m_sourceType),
						 ("Played stun reaction but not got valid anim instance!"));
		}

		AiLogAnim(pNavChar, "No hit reaction instance, nulling out m_pLastFullBodyHitEntry\n");
		m_pLastFullBodyHitEntry		   = nullptr;
		m_lastFullBodyHitReactionState = 0;
	}

	if (m_pLastFullBodyHitEntry && m_pLastFullBodyHitEntry->m_groundAdjustWindow && m_fullBodyAnimAction.IsValid()
		&& !m_fullBodyAnimAction.IsDone() && pHitReactionInst)
	{
		static const float kHitReactionGroundAdjustRate = 2.5f;

		const float phase = pHitReactionInst->Phase();
		if (phase <= m_pLastFullBodyHitEntry->m_groundAdjustWindow->m_val1
			&& phase >= m_pLastFullBodyHitEntry->m_groundAdjustWindow->m_val0)
		{
			Seek(m_groundAdjustFactor, 1.0f, dt * kHitReactionGroundAdjustRate);
		}
		else
		{
			Seek(m_groundAdjustFactor, 0.0f, dt * kHitReactionGroundAdjustRate);
		}
	}
	else
	{
		const bool disableGroundAdjust = m_pLastFullBodyHitEntry
											 ? m_pLastFullBodyHitEntry->m_flags & DC::kHitReactionFlagsNoGroundAdjust
											 : false;
		const float des = disableGroundAdjust ? 0.0f : 1.0f;
		if (m_groundAdjustFactor != des)
		{
			m_groundAdjustFactor = des;
		}
	}

	if (m_pLastFullBodyHitEntry && (m_pLastFullBodyHitEntry->m_flags & DC::kHitReactionFlagsNoAdjustToNav)
		&& m_fullBodyAnimAction.IsValid() && !m_fullBodyAnimAction.IsDone() && pHitReactionInst)
	{
		DC::AnimStateFlag& hrStateFlags = pHitReactionInst->GetMutableStateFlags();
		hrStateFlags |= DC::kAnimStateFlagNoAdjustToNavMesh;
	}

	// If ragdoll is setup to keep motors active after anim ends we want to make sure to turn them off after some time so that we can swap for process ragdoll and become cheap
	if (m_ragdollKeepMotors)
	{
		if (m_postDeathAnimDelay)
		{
			m_postDeathAnimDelay--;
			if (!m_postDeathAnimDelay)
			{
				if (CompositeBody* pRagdoll = pNavChar->GetRagdollCompositeBody())
				{
					pRagdoll->SetAnimBlend(0.0f); // make sure we're also fully blended into physics since the anim will play in some irrelevant WS location
				}
				DieSpecial(m_postDeathAnim,  nullptr, INVALID_STRING_ID_64, 0.0f);
				m_ragdollSettingsDelay = 2; // allow transition to post death anim and avoid ragdoll picking up any acceleration from the transition
			}
		}
		else
		{
			CompositeRagdollController* pRagdolCtrl = pNavChar->GetRagdollController();
			if (m_ragdollSettingsDelay)
			{
				m_ragdollSettingsDelay--;
				if (!m_ragdollSettingsDelay)
				{
					if (pRagdolCtrl)
						pRagdolCtrl->SetControlSettings(m_ragdollSettings);
				}
			}
			if (m_deathAnimEndTime != TimeFrameNegInfinity())
			{
				// This is just a hardcoded value that fits our 2015 E3 Demo convoy scenario
				if ((pNavChar->GetCurTime() - m_deathAnimEndTime).ToSeconds() > 8.0f)
				{
					pRagdolCtrl->SetKeepMotors(false);
					m_ragdollKeepMotors = false;
				}
			}
			else if (!m_fullBodyAnimAction.IsValid() || m_fullBodyAnimAction.IsDone()
					 || m_fullBodyAnimAction.GetAnimPhase(pNavChar->GetAnimControl()) > 0.99f)
			{
				m_deathAnimEndTime = pNavChar->GetCurTime();
				if (m_turnOffMotorsAtAnimEnd)
				{
					pRagdolCtrl->SetKeepMotors(false);
					pRagdolCtrl->PowerOff("", g_ragdollMotorSettings.m_maxForceEndAnimLerpTime); // to apply the lerp for anim end
					m_ragdollKeepMotors = false;
				}
			}
		}
	}
	else
	{
		if (m_pLastFullBodyHitEntry && m_pLastFullBodyHitEntry->m_earlyRagdollPowerOffSettings && pNavChar->IsRagdollDying())
		{
			if (CompositeRagdollController* pRagdolCtrl = pNavChar->GetRagdollController())
			{
				// We do some additional checks because sometimes IsBusy reports true even if we finished playing anim (when fadeOutPhase is specified)
				if (!IsBusy() || ((!m_fullBodyAnimAction.IsValid() || m_fullBodyAnimAction.IsDone()
					 || m_fullBodyAnimAction.GetAnimPhase(pNavChar->GetAnimControl()) > 0.99f)))
				{
					// Reached end of anim, turn motors off
					pRagdolCtrl->SetKeepMotors(false);
				}
				else if (!pRagdolCtrl->GetPowerOffCheck())
				{
					// If we powered off early, play requested animation and schedule motors to turn off at the end
					PostDeathAnim(m_pLastFullBodyHitEntry->m_earlyRagdollPowerOffAnim,
								  m_pLastFullBodyHitEntry->m_earlyRagdollPowerOffSettings);
					m_pLastFullBodyHitEntry = nullptr;
					m_pLastSelectedHitEntry		   = nullptr;
					m_lastFullBodyHitReactionState = 0;
				}
			}
		}
	}

	if (m_interruptSkills)
	{
		// stop interrupting skills when hit reaction is fading out
		if (!m_fullBodyAnimAction.IsTopInstance(pAnimControl)
			&& !IsStunReactionPlaying()) // we don't want to do this for stun anims
		{
			m_interruptSkills = false;
		}
		else if ((m_exitPhase >= 0.0f) && m_fullBodyAnimAction.HasTransitionBeenProcessed())
		{
			static const float kSkillEvalFrameBuffer = 4.0f;
			const float duration  = m_fullBodyAnimAction.GetDuration(pAnimControl);
			const float animPhase = m_fullBodyAnimAction.GetAnimPhase(pAnimControl);
			const float skillResumePhaseShift = Max((dt * kSkillEvalFrameBuffer) / duration, 0.0f);
			const float skillResumePhase	  = m_exitPhase - skillResumePhaseShift;

			if (animPhase >= 0.0f && animPhase >= skillResumePhase)
			{
				m_interruptSkills	  = false;
				m_interruptNavigation = false;
			}
		}

		if (const Npc* pNpc = Npc::FromProcess(pNavChar))
		{
			pNpc->PrimeHitReactionPostSelector();
		}
	}

	m_handFixIkArg.m_tt = 0.0f;
	m_handFixIkArg.m_handsToIk[1] = false;
	m_handFixIkArg.m_handsToIk[0] = false;

	// 	if (pNavChar->GetCurrentMeleeAction())
	// 	{
	// 		const DC::MeleeEvent *pEvent = pNavChar->GetCurrentMeleeAction()->GetActiveEventByType(DC::kMeleeEventTypeHandFix);
	//
	// 		if (pEvent)
	// 		{
	// 			m_handFixIkArg.m_tt = 1.0f;
	// 			m_handFixIkArg.m_handsToIk[0] = pEvent->m_flags & DC::kMeleeEventFlagFixLeftHand;
	// 			m_handFixIkArg.m_handsToIk[1] = pEvent->m_flags & DC::kMeleeEventFlagFixRightHand;
	// 		}
	// 	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// For playing another anim with special ragdoll settings after death anim has ended or had to be interrupted due to
// ragdoll collision. The ragdoll settings is expected to not care about WS position of the anim
void AiHitController::PostDeathAnim(StringId64 anim, StringId64 ragdollSettings)
{
	NavCharacter* pNavChar = GetCharacter();
	if (!pNavChar)
		return;

	CompositeBody* pRagdoll = pNavChar->GetRagdollCompositeBody();
	if (!pRagdoll)
		return;

	CompositeRagdollController* pRagdolCtrl = pRagdoll->GetRagdollController();
	if (!pRagdolCtrl)
		return;

	if (!anim)
	{
		// Just keep playing current anim
	}
	else
	{
		bool ragdollFullyPhysicalized = true;
		for (U32 ii = 0; ii<pRagdoll->GetNumBodies(); ii++)
		{
			if (pRagdoll->GetBody(ii)->GetHavokMotionType() != kRigidBodyMotionTypePhysicsDriven)
			{
				ragdollFullyPhysicalized = false;
				break;
			}
		}

		if (ragdollFullyPhysicalized)
		{
			DieSpecial(anim,
					   nullptr,
					   INVALID_STRING_ID_64,
					   0.0f);
			pRagdoll->SetAnimBlend(0.0f); // make sure we're also fully blended into physics since the anim will play in some irrelevant WS location
		}
		else
		{
			m_postDeathAnim = anim;
			m_postDeathAnimDelay = 2;
		}
	}

	m_ragdollKeepMotors		 = true;
	m_turnOffMotorsAtAnimEnd = true;
	pRagdolCtrl->SetKeepMotors(true);
	pRagdolCtrl->PowerOff("PostDeath Anim", 0.6f); // in case we haven't powered off yet
	pNavChar->SetRagdollGroundProbesEnabled(false);
	pNavChar->GetRagdollCompositeBody()->SetLocalTargetPose(false); // in case this has already been set due to no death anim playing

	if (ragdollSettings)
	{
		m_ragdollSettings = ragdollSettings;
		if (!anim)
		{
			pRagdolCtrl->SetControlSettings(ragdollSettings, 0.0f);
		}
		else
		{
			m_ragdollSettingsDelay = 2; // we need to delay this to make sure anim has started and transitioned to avoid picking up any acceleration form the transition
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiHitController::GetRagdollBlendOutHoleDepth() const
{
	if (m_pLastFullBodyHitEntry && m_fullBodyAnimAction.IsValid() && !m_fullBodyAnimAction.IsDone())
	{
		return m_pLastFullBodyHitEntry->m_ragdollBlendoutHoleDepth;
	}
	return 0.5f; // default
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsBusy() const
{
	bool actionBusy = m_fullBodyAnimAction.IsValid() && !m_fullBodyAnimAction.IsDone();

	// NB: When full-body hit reactions exit and fade-out on the same frame, the locomotion controller may try to
	//    transition to movement without a handoff. To avoid this race condition, the controller is kept busy
	//    until a handoff has been set (when m_pLastFullBodyHitEntry is cleared), before relinquishing control.

	if (m_pLastFullBodyHitEntry && (m_lastFullBodyFadeOutPhase >= 0.0f))
	{
		actionBusy = true;
	}
	else if ((m_exitPhase >= 0.0f) && m_fullBodyAnimAction.HasTransitionBeenProcessed())
	{
		const float animPhase = m_fullBodyAnimAction.GetAnimPhase(GetCharacter()->GetAnimControl());

		if (animPhase >= 0.0f)
		{
			actionBusy = animPhase < m_exitPhase;
		}
	}

	const bool beingShoved = IsBeingShoved();

	const bool doingRecovery = m_recoveryAnimAction.IsValid() && !m_recoveryAnimAction.IsDone();

	const bool busy = actionBusy || beingShoved || doingRecovery;

	return busy;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsHitReactionPlaying() const
{
	if (m_fullBodyAnimAction.IsValid() && !m_fullBodyAnimAction.IsDone())
		return true;

	if (m_partialAnimAction.IsValid() && !m_partialAnimAction.IsDone())
		return true;

	if (m_partialOverrideAnimAction.IsValid() && !m_partialOverrideAnimAction.IsDone())
		return true;

	if (m_additiveAnimAction.IsValid() && !m_additiveAnimAction.IsDone())
		return true;

	const AnimControl* pAnimControl = GetCharacter()->GetAnimControl();
	if (const AnimSimpleLayer* hitReactionLayer = pAnimControl->GetSimpleLayerById(SID("hit-reaction-partial")))
	{
		if (hitReactionLayer->IsValid() && hitReactionLayer->GetCurrentFade() > 0.0f)
			return true;
	}

	if (const AnimStateLayer* hitReactionLayer = pAnimControl->GetStateLayerById(SID("hit-reaction-partial-override")))
	{
		if (hitReactionLayer->IsValid() && hitReactionLayer->GetCurrentFade() > 0.0f)
			return true;
	}

	if (const AnimSimpleLayer* hitReactionLayer = pAnimControl->GetSimpleLayerById(SID("hit-reaction-additive")))
	{
		if (hitReactionLayer->IsValid() && hitReactionLayer->GetCurrentFade() > 0.0f)
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsExplosionFullBodyReactionPlaying() const
{
	if (m_fullBodyAnimAction.IsValid() && !m_fullBodyAnimAction.IsDone())
	{
		const AnimControl* pAnimControl = GetCharacter()->GetAnimControl();
		return m_fullBodyAnimAction.GetPhaseAnimNameId(pAnimControl) == m_lastExplosionReactionAnim;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsFullBodyReactionPlaying(DC::HitReactionStateMask mask /*=0*/) const
{
	if (m_fullBodyAnimAction.IsValid() && !m_fullBodyAnimAction.IsDone())
	{
		if (m_pLastFullBodyHitEntry && mask)
		{
			const DC::HitReactionStateMask matchingMask = m_lastFullBodyHitReactionState & mask;
			return matchingMask == mask;
		}

		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
TimeFrame AiHitController::GetLastStunPlayingTime() const
{
	return m_lastStunPlayingTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsStunReactionFromSmokeOrStunBombPlaying() const
{
	if (m_fullBodyAnimAction.IsValid() && !m_fullBodyAnimAction.IsDone())
	{
		if (m_lastFullBodySourceType & DC::kHitReactionSourceTypeSmokeOrStunBomb)
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsStunReactionFromPlayerSmokeOrStunBombPlaying() const
{
	if (m_fullBodyAnimAction.IsValid() && !m_fullBodyAnimAction.IsDone())
	{
		if (m_lastFullBodySourceType & DC::kHitReactionSourceTypeSmokeOrStunBomb)
		{
			if (m_lastFullBodyFromPlayer)
				return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsStunReactionFromThrownObjectPlaying() const
{
	return IsStunReactionFromBuddyThrownObjectPlaying() || IsStunReactionFromNonBuddyThrownObjectPlaying();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsStunReactionFromNonBuddyThrownObjectPlaying() const
{
	if (m_fullBodyAnimAction.IsValid() && !m_fullBodyAnimAction.IsDone())
	{
		if (m_lastFullBodySourceType & DC::kHitReactionSourceTypeThrownObject)
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsStunReactionFromBuddyThrownObjectPlaying() const
{
	if (m_fullBodyAnimAction.IsValid() && !m_fullBodyAnimAction.IsDone())
	{
		if (m_lastFullBodySourceType & DC::kHitReactionSourceTypeBuddyThrownObject)
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsStunReactionPlaying() const
{
	if (m_fullBodyAnimAction.IsValid() && !m_fullBodyAnimAction.IsDone())
	{
		if (IsHitReactionSourceTypeStun(m_lastFullBodySourceType))
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsStunKneeReactionPlaying() const
{
	if (m_fullBodyAnimAction.IsValid() && !m_fullBodyAnimAction.IsDone())
	{
		if (IsHitReactionSourceTypeStun(m_lastFullBodySourceType))
		{
			StringId64 bucket = GetCurrentActiveBucket();
			if (bucket == SID("ground"))
			{
				return true;
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsInStrikePhaseRange() const
{
	if (m_fullBodyAnimAction.IsValid() && !m_fullBodyAnimAction.IsDone())
	{
		if (m_pLastSelectedHitEntry)
		{
			if (const DC::AiRangeval* pStrikeRange = m_pLastSelectedHitEntry->m_strikePhaseRange)
			{
				const NavCharacter* pNavChar	= GetCharacter();
				const AnimControl* pAnimControl = pNavChar->GetAnimControl();
				const F32 curPhase = m_fullBodyAnimAction.GetAnimPhase(pAnimControl);
				if (pStrikeRange->m_val0 > curPhase || pStrikeRange->m_val1 < curPhase)
				{
					return false;
				}
			}
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiHitController::GetSecInStunReaction() const
{
	if (m_fullBodyAnimAction.IsValid() && !m_fullBodyAnimAction.IsDone())
	{
		if (IsHitReactionSourceTypeStun(m_lastFullBodySourceType))
		{
			const NavCharacter* pNavChar	= GetCharacter();
			const AnimControl* pAnimControl = pNavChar->GetAnimControl();
			const float phase = m_fullBodyAnimAction.GetAnimPhase(pAnimControl);
			if (phase == -1.0f)
				return -1.0f;
			const float duration = m_fullBodyAnimAction.GetDuration(pAnimControl);
			if (duration == -1.0f)
				return -1.0f;
			return phase * duration;
		}
	}

	return -1.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsAdditiveHitReactionPlaying() const
{
	if (m_additiveAnimAction.IsValid() && !m_additiveAnimAction.IsDone())
	{
		return true;
	}

	return false;
}

StringId64 AiHitController::GetAdditiveAnim() const
{
	return m_additiveAnimAction.GetPhaseAnimNameId(GetCharacter()->GetAnimControl());
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsAdditiveHitReactionPausingMelee() const
{
	if (m_partialOverrideAnimAction.IsValid() && !m_partialOverrideAnimAction.IsDone())
	{
		// 		if (m_partialAnimAction.GetAnimFrame(GetCharacter()->GetAnimControl()) < numFrames)
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiHitController::GetOverrideHitPartial() const
{
	if (m_partialOverrideAnimAction.IsValid() && !m_partialOverrideAnimAction.IsDone())
	{
		return m_partialOverrideAnimAction.GetPhaseAnimNameId(GetCharacterGameObject()->GetAnimControl());
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::SetPartialOverride(StringId64 part, F32 frameStart)
{
	m_partialOverride	   = part;
	m_partialOverrideFrame = frameStart;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsFireBackGesturePlaying() const
{
	if (!IsHitReactionPlaying())
	{
		return false;
	}

	const bool fullBodyHitIsFireBack = m_pLastFullBodyHitEntry
									   && m_pLastFullBodyHitEntry->m_fireBackGesture != INVALID_STRING_ID_64
									   && (m_pLastFullBodyHitEntry->m_flags & DC::kHitReactionFlagsFireBackOnly) != 0;
	const bool lastHitIsFireBack = m_pLastSelectedHitEntry
								   && m_pLastSelectedHitEntry->m_fireBackGesture != INVALID_STRING_ID_64
								   && (m_pLastSelectedHitEntry->m_flags & DC::kHitReactionFlagsFireBackOnly) != 0;

	return fullBodyHitIsFireBack || lastHitIsFireBack;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::ShouldInterruptNavigation() const
{
	if (m_pLastFullBodyHitEntry)
	{
		switch (m_pLastFullBodyHitEntry->m_exitMode)
		{
		case DC::kHitReactionExitModeCover:
		case DC::kHitReactionExitModePerch:
			if (m_hRecoveryAp.IsValid())
			{
				return true;
			}
			break;
		}
	}

	if (!IsBusy())
	{
		return false;
	}

	if (g_netInfo.IsNetActive() && !GetCharacter()->IsNetOwnedByMe())
	{
		return IsBeingShoved();
	}

	return m_interruptNavigation || IsBeingShoved();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::ShouldInterruptSkills() const
{
	// NB: must interrupt during recovery so that we do not re-enter skills
	// in a very bad state: where the cover controller still considers us in cover,
	// but the nav state machine (interrupted) does not yet.
	//
	// if we wait until recovery completes, recovery will SwitchToActionPack() so that
	// the state is consistent again when it relinquishes control back to skills.
	if (m_pLastFullBodyHitEntry)
	{
		switch (m_pLastFullBodyHitEntry->m_exitMode)
		{
		case DC::kHitReactionExitModeCover:
		case DC::kHitReactionExitModePerch:
			if (m_hRecoveryAp.IsValid())
			{
				return true;
			}
			break;
		}
	}

	if (!IsBusy())
	{
		return false;
	}

	if (g_netInfo.IsNetActive() && !GetCharacter()->IsNetOwnedByMe())
	{
		return IsBeingShoved();
	}

	return m_interruptSkills || IsBeingShoved();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::HitReactionEntryList* AiHitController::GetHitAnimationList(StringId64 setId, StringId64 bucketId) const
{
	if (setId == INVALID_STRING_ID_64 || bucketId == INVALID_STRING_ID_64)
		return nullptr;

	const DC::Map* pHitMap = ScriptManager::Lookup<const DC::Map>(setId, nullptr);
	if (!pHitMap)
		return nullptr;

	return ScriptManager::MapLookup<DC::HitReactionEntryList>(pHitMap, bucketId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::HitReactionEntryList* AiHitController::GetDeathAnimationList(StringId64 setId, StringId64 bucketId) const
{
	if (setId == INVALID_STRING_ID_64 || bucketId == INVALID_STRING_ID_64)
		return nullptr;

	const DC::Map* pDeathMap = ScriptManager::Lookup<const DC::Map>(setId, nullptr);
	if (!pDeathMap)
		return nullptr;

	return ScriptManager::MapLookup<DC::HitReactionEntryList>(pDeathMap, bucketId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::AllHitAnimsDisabled(DC::HitReactionSourceType srcType)
{
	const Npc* pNpc = Npc::FromProcess(GetCharacter());
	if (!pNpc)
		return false;

	const Skill* pSkill = pNpc->GetActiveSkill();
	if (!pSkill)
		return false;

	if (!pSkill->AllowHitReactiton(pNpc))
		return true;

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::PlayHitAnim(const PlayAnimParams& params)
{
	m_fullBodyAnimAction.ClearSelfBlendParams();

	m_lastReactionWasBlock = false;

	NavCharacter* pChar		  = GetCharacter();
	AnimControl* pAnimControl = pChar->GetAnimControl();
	DC::AnimNpcInfo* pInfo	  = pAnimControl->Info<DC::AnimNpcInfo>();
	DC::AnimInstanceInfo* pInstanceInfo = pAnimControl->InstanceInfo<DC::AnimInstanceInfo>();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	const ArtItemAnim* pArtItem = pAnimControl->LookupAnim(params.m_animId).ToArtItem();
	if (!pArtItem)
	{
		MsgAnimErr("-----------------------------------------------------------------\n");
		MsgAnimErr("Npc(%s) tried to play hit reaction that doesn't exist!\n                  %s\n",
				   pChar->GetName(),
				   DevKitOnly_StringIdToString(params.m_animId));
		MsgAnimErr("-----------------------------------------------------------------\n");
		return false;
	}

	AI::SetPluggableAnim(pChar, params.m_animId, params.m_mirror);

	float blendTimeIntoHitReaction = IsBusy() ? 0.3f : 0.4f;

	if (params.m_blendTime >= 0.0f)
	{
		blendTimeIntoHitReaction = params.m_blendTime;
	}

	const StringId64 stateId = (params.m_gestureId != INVALID_STRING_ID_64) ? SID("s_hit-reaction-w-gesture-state")
																			: SID("s_hit-reaction-state");

	pInfo->m_hitReactionGesture = params.m_gestureId;

	AiLogAnim(pChar,
			  "Playing Hit Anim '%s'%s ap:%s handoff:%s\n",
			  DevKitOnly_StringIdToString(params.m_animId),
			  params.m_mirror ? " (Flipped)" : "",
			  PrettyPrint(params.m_frame),
			  GetHandoffTypeStr(params.m_handoffType));

	SendEvent(SID("stop-animating"), pChar, DC::kAnimateLayerAll, 0.0f, INVALID_STRING_ID_64);

	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	FadeToStateParams fadeParams;
	fadeParams.m_stateStartPhase = params.m_startPhase;
	fadeParams.m_apRef		  = params.m_frame;
	fadeParams.m_apRefValid	  = true;
	fadeParams.m_animFadeTime = blendTimeIntoHitReaction;
	fadeParams.m_motionFadeTime = params.m_motionBlendTime;
	fadeParams.m_customApRefId	= params.m_customApRefId;

	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_assertOnStunFailure))
	{
		if (m_pLastFullBodyHitEntry && IsHitReactionSourceTypeStun(m_pLastFullBodyHitEntry->m_sourceType))
		{
			fadeParams.m_assertOnFailure = true;
		}
	}
	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_assertOnFailure))
	{
		fadeParams.m_assertOnFailure = true;
	}

	params.m_pAnimAction->FadeToState(pAnimControl, stateId, fadeParams, AnimAction::kFinishOnAnimEnd);

	NavAnimHandoffDesc handoff;
	handoff.SetStateChangeRequestId(params.m_pAnimAction->GetStateChangeRequestId(), stateId);
	handoff.m_steeringPhase = -1.0f;

	bool doHandoff = true;

	switch (params.m_handoffType)
	{
	case HandoffType::kNone:
		doHandoff = false;
		break;

	case HandoffType::kMoving:
		handoff.m_motionType = pChar->GetRequestedMotionType();
		break;
	}

	if (doHandoff)
	{
		pChar->ConfigureNavigationHandOff(handoff, FILE_LINE_FUNC);
	}

	pInfo->m_hitReactionScale	= params.m_playbackRate;
	pInfo->m_hitReactionDtgroup = params.m_dtGroupId;

	m_exitPhase = params.m_exitPhase;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::PlayDeathAnim(const PlayAnimParams& params)
{
	NavCharacter* pChar		  = GetCharacter();
	AnimControl* pAnimControl = pChar->GetAnimControl();
	DC::AnimNpcInfo* pInfo	  = pAnimControl->Info<DC::AnimNpcInfo>();
	DC::AnimInstanceInfo* pInstanceInfo = pAnimControl->InstanceInfo<DC::AnimInstanceInfo>();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	const bool isOnVehicle = pChar->GetEnteredVehicle().Valid();
	bool playAnimOnHorse = false;

	const Horse* pHorse = nullptr;
	if (const IAiRideHorseController* pRideHorse = pChar->GetAnimationControllers()->GetRideHorseController())
	{
		pHorse = pRideHorse->GetHorse();
		playAnimOnHorse = pHorse != nullptr;
	}

	// we should act as if bound to a horse for the purposes of dying on a horse if we are playing an anim relative to a horse
	if (const NdSubsystemMgr* pSubsytemMgr = pChar->GetSubsystemMgr())
	{
		const AnimStateInstance* pCurrentAnim = pBaseLayer->CurrentStateInstance();
		if (pCurrentAnim)
		{
			const BindToHorseAnimAction* pBindToHorseAnimAction = static_cast<const BindToHorseAnimAction*>(pSubsytemMgr->FindBoundSubsystemAnimActionByType(pCurrentAnim, SID("BindToHorseAnimAction")));
			pHorse = pBindToHorseAnimAction ? pBindToHorseAnimAction->GetHorse() : nullptr;
		}
	}

	const bool keepRagdollWithHorse = pHorse != nullptr;

	m_exitPhase = params.m_exitPhase;

	AI::SetPluggableAnim(pChar, params.m_animId, params.m_mirror);

	pInfo->m_hitReactionScale = params.m_playbackRate;

	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	StringId64 stateId = INVALID_STRING_ID_64;
	if (isOnVehicle)
	{
		stateId = SID("s_death-vehicle-state");
	}
	else if (params.m_fromHorseDeath)
	{
		stateId = SID("s_synced-death-on-horse-state");
	}
	else if (playAnimOnHorse)
	{
		stateId = SID("s_death-on-horse-state");
	}
	else if (params.m_gestureId != INVALID_STRING_ID_64)
	{
		stateId = SID("s_death-w-gesture-state");
	}
	else
	{
		stateId = SID("s_death-state");
	}

	// this is a pretty lame place to do this, but I need to know which anim the horse is playing any time it dies systemically
	if (Horse* pHorseSelf = Horse::FromProcess(pChar))
	{
		pHorseSelf->KillNpcRiders(params.m_animId, params.m_mirror);
	}

	pInfo->m_hitReactionGesture = params.m_gestureId;
	pInfo->m_hitReactionDtgroup = params.m_dtGroupId;

	const StringId64 deathLoopId = pChar->GetDeathLoopAnimId();
	if ((pInfo->m_hitReactionDtgroup == INVALID_STRING_ID_64) && (deathLoopId != INVALID_STRING_ID_64))
	{
		pInfo->m_deathLoopAnim = deathLoopId;
		pInfo->m_hitReactionDtgroup = SID("death-exit^loop");
	}

	const float blendTimeIntoHitReaction = params.m_blendTime >= 0.0f ? params.m_blendTime : 0.25f;

	FadeToStateParams fadeParams;
	fadeParams.m_stateStartPhase = params.m_startPhase;
	fadeParams.m_apRef		  = params.m_frame;
	fadeParams.m_apRefValid	  = true;
	fadeParams.m_animFadeTime = blendTimeIntoHitReaction;
	fadeParams.m_motionFadeTime = blendTimeIntoHitReaction;
	fadeParams.m_blendType		= DC::kAnimCurveTypeUniformS;
	fadeParams.m_customApRefId	= params.m_customApRefId;

	params.m_pAnimAction->FadeToState(pAnimControl, stateId, fadeParams, AnimAction::kFinishOnAnimEnd);

	const bool noGroundProbe = (m_pLastFullBodyHitEntry && (m_pLastFullBodyHitEntry->m_flags & DC::kHitReactionFlagsNoRagdollGroundProbes))
							   || stateId == SID("s_death-on-horse-state") || stateId == SID("s_synced-death-on-horse-state");
	if (noGroundProbe || isOnVehicle)
	{
		pChar->SetRagdollGroundProbesEnabled(false);
	}

	if (isOnVehicle || keepRagdollWithHorse)
	{
		// Special settings for ragdolls on vehicle
		pChar->SetRagdollDyingSettings(SID("*vehicle-deaths-ragdoll-control-settings*"), 0.0f);
		if (CompositeBody* pRagdollBody = pChar->GetRagdollCompositeBody())
		{
			// We want to keep motors on these guys
			pRagdollBody->GetRagdollController()->SetKeepMotors(true);
			m_ragdollKeepMotors = true;

			if (keepRagdollWithHorse && !params.m_fromHorseDeath)
			{
				GAMEPLAY_ASSERT(pHorse);
				const RigidBody* pHorseBody = pHorse->GetCompositeBody()->GetBody(0);
				ANIM_ASSERT(pHorseBody);
				pRagdollBody->SetParentBody(pHorseBody);
				pRagdollBody->SetAutoUpdateParentBody(false);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* AiHitController::GetDirectionMaskName(U32F dirMask) const
{
	StringBuilder<256>* pBuf = NDI_NEW(kAllocSingleGameFrame) StringBuilder<256>();

	const DC::EnumNames* pStateNames
		= ScriptManager::LookupInModule<const DC::EnumNames>(SID("*hit-reaction-direction-mask-names*"),
															 SID("hit-reactions"),
															 nullptr);

	U32F remainingMask = dirMask;

	if (pStateNames)
	{
		for (U32F i = 0; i < pStateNames->m_count; ++i)
		{
			const DC::HitReactionStateMask maskVal = 1ULL << i;

			if ((dirMask & maskVal) == 0)
				continue;

			pBuf->append_format("%s ", pStateNames->m_array[i].GetString());

			remainingMask &= ~maskVal;
		}
	}

	if (remainingMask)
	{
		pBuf->append_format("<unknown-dirs");
		while (remainingMask != 0)
		{
			const I32 bitIndex = FindFirstSetBitIndex(remainingMask);
			pBuf->append_format(" %d", bitIndex);
			remainingMask = remainingMask & ~(1ULL << U64(bitIndex));
		}
		pBuf->append_format(">");
	}

	return pBuf->c_str();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* AiHitController::GetAttackTypeName(DC::HitReactionSourceType attackType) const
{
	StringBuilder<256>* pBuf = NDI_NEW(kAllocSingleGameFrame) StringBuilder<256>();

	const DC::EnumNames* pSourceNames
		= ScriptManager::LookupInModule<const DC::EnumNames>(SID("*hit-reaction-source-type-names*"),
															 SID("hit-reactions"),
															 nullptr);

	DC::HitReactionSourceType remainingMask = attackType;

	if (pSourceNames)
	{
		for (U32F i = 0; i < pSourceNames->m_count; ++i)
		{
			const DC::HitReactionSourceType maskVal = 1ULL << i;

			if ((attackType & maskVal) == 0)
				continue;

			pBuf->append_format("%s ", pSourceNames->m_array[i].GetString());

			remainingMask &= ~maskVal;
		}
	}

	if (remainingMask)
	{
		pBuf->append_format("<unknown-source");
		while (remainingMask != 0)
		{
			const I32 bitIndex = FindFirstSetBitIndex(remainingMask);
			pBuf->append_format(" %d", bitIndex);
			remainingMask = remainingMask & ~(1ULL << U64(bitIndex));
		}
		pBuf->append_format(">");
	}

	return pBuf->c_str();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* AiHitController::GetNpcStateName(DC::HitReactionStateMask npcState) const
{
	StringBuilder<256>* pBuf = NDI_NEW(kAllocSingleGameFrame) StringBuilder<256>();

	if (!pBuf)
		return "";

	DC::HitReactionStateMask remainingMask = npcState;

	const DC::EnumNames* pStateNames = ScriptManager::LookupInModule<const DC::EnumNames>(SID("*hit-reaction-state-mask-names*"),
																						  SID("hit-reactions"),
																						  nullptr);

	if (pStateNames)
	{
		for (U32F i = 0; i < pStateNames->m_count; ++i)
		{
			const DC::HitReactionStateMask maskVal = 1ULL << i;

			if ((npcState & maskVal) == 0)
				continue;

			pBuf->append_format("%s ", pStateNames->m_array[i].GetString());

			remainingMask &= ~maskVal;
		}
	}

	if (remainingMask)
	{
		pBuf->append_format("<unknown-states");
		while (remainingMask != 0)
		{
			const I32 bitIndex = FindFirstSetBitIndex(remainingMask);
			pBuf->append_format(" %d", bitIndex);
			remainingMask = remainingMask & ~(1ULL << U64(bitIndex));
		}
		pBuf->append_format(">");
	}

	return pBuf->c_str();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const TimeFrame AiHitController::GetLastHitTime() const
{
	return m_lastHitTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::SetLastHitTime(TimeFrame time)
{
	m_lastHitTime = time;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Ap-ref orientation is arbitrary as long as extents match an edge and the animation uses the same ap-ref orientation.
// Ap-ref will adjust to closest pos on hint extents.  Anim may be offset from ap-ref.  Char must be within a tolerance
// of the adjusted anim start.  Char must match anim start facing within a tolerance.
float AiHitController::RateDeathHint(const DeathHint* pHint, BoundFrame* pApRefOut) const
{
	const float kMaxDistSqr	 = 1.0f;
	const float kMinDotAngle = Cos(DegreesToRadians(84.0f));

	const StringId64 animId = pHint->GetAnim();
	if (INVALID_STRING_ID_64 == animId)
		return -kLargeFloat;

	BoundFrame apRef	   = pHint->GetBoundFrame();
	const Character* pChar = GetCharacter();
	AnimControl* pAnimControl = pChar->GetAnimControl();

	Locator alignWs = Locator(kIdentity);
	if (!FindAlignFromApReference(pAnimControl, animId, apRef.GetLocatorWs(), &alignWs))
		return -kLargeFloat;

	// Find the closest pos on the hint extents for the ap-ref where the anim start is as close to the char as possible
	const Vector apRefToalignWs = alignWs.GetTranslation() - apRef.GetTranslation();
	const Point charPosWs		= pChar->GetTranslation();
	const Point closestPosWs	= pHint->GetClosestPosWs(charPosWs - apRefToalignWs);

	apRef.SetTranslationWs(closestPosWs);

	const Point offsetAlignWs = apRef.GetTranslation() + apRefToalignWs;

	// The character must be near the position of the start of the animation
	const float distSqr = DistSqr(charPosWs, offsetAlignWs);
	if (distSqr > kMaxDistSqr)
		return -kLargeFloat;

	const Vector alignZ = GetLocalZ(alignWs.Rot());
	const Vector charZ	= GetLocalZ(pChar->GetLocator().Rot());

	// The character must be near the direction of the start of the animation
	const float dotP = Dot(alignZ, charZ);
	if (dotP < kMinDotAngle)
		return -kLargeFloat;

	const float rating = dotP + kMaxDistSqr - distSqr;

	if (pApRefOut)
		*pApRefOut = apRef;

	return rating;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::DieWithDeathHint()
{
	NavCharacter* pNavChar = static_cast<NavCharacter*>(GetCharacter());

	if (pNavChar->IsUsingTraversalActionPack())
		return false;

	const Point myPosWs = pNavChar->GetTranslation();

	DeathHintHandle hints[32];
	const U32F numHints = DeathHintMgr::FindDeathHints(myPosWs, 5.0f, hints, 32);

	float bestRating	 = -kLargeFloat;
	BoundFrame bestApRef = BoundFrame(kIdentity);
	const DeathHint* pBestHint = nullptr;

	for (U32F i = 0; i < numHints; ++i)
	{
		const DeathHint* pHint = hints[i].ToProcess();
		BoundFrame apRef;
		const float rating = RateDeathHint(pHint, &apRef);

		if (rating > bestRating)
		{
			pBestHint  = pHint;
			bestRating = rating;
			bestApRef  = apRef;
		}
	}

	if (pBestHint)
	{
		AnimControl* pAnimControl = pNavChar->GetAnimControl();

		const StringId64 animId = pBestHint->GetAnim();
		AI::SetPluggableAnim(pNavChar, animId);

		const float blendTimeIntoHitReaction = IsBusy() ? 0.3f : 0.1f;

		FadeToStateParams params;
		params.m_apRef		  = bestApRef;
		params.m_apRefValid	  = true;
		params.m_animFadeTime = blendTimeIntoHitReaction;
		params.m_motionFadeTime = 0.4f;
		params.m_blendType		= DC::kAnimCurveTypeEaseIn;

		m_fullBodyAnimAction.FadeToState(pAnimControl,
										 SID("s_death-state"),
										 params,
										 AnimAction::kFinishOnTransitionTaken);
		m_exitPhase = 1.0f;

		if (const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animId).ToArtItem())
		{
			const float kBlendTimeIntoRagdoll = 0.3f;

			const float duration = GetDuration(pAnim);
			const float delay	 = Max(duration - kBlendTimeIntoRagdoll, 0.0f);

			pNavChar->SetRagdollDelay(delay);
		}

		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::DieOnVehicleAsDriver()
{
	Npc* pNpc = Npc::FromProcess(GetCharacter());
	if (!pNpc)
		return false;

	const NpcHandle hNpc = pNpc;

	const NdGameObject* pVehicle = NdGameObject::FromProcess(GetCharacter()->GetEnteredVehicle().ToProcess());
	if (!pVehicle)
		return false;

	// get rail controller
	const RailController* pRailController = nullptr;
	if (const DriveableVehicle* pDriveableVehicle = DriveableVehicle::FromProcess(pVehicle))
	{
		pRailController = pDriveableVehicle->GetRailController();
	}
	else if (const RailVehicle* pRailVehicle = RailVehicle::FromProcess(pVehicle))
	{
		pRailController = pRailVehicle->GetRailController();
	}
	if (!pRailController)
		return false;

	// determine rider slot
	// only the driver pulls death animations from rail controller when killed on vehicle
	RailController::AnimationSlot animSlot = RailController::kSlotCount;
	if (hNpc == pRailController->GetDriver())
	{
		animSlot = RailController::kSlotDriver;
	}
	// 	else if (hNpc == pRailController->GetPassenger1())
	// 	{
	// 		animSlot = RailController::kSlotPassenger1;
	// 	}
	// 	else if (hNpc == pRailController->GetPassenger2())
	// 	{
	// 		animSlot = RailController::kSlotPassenger2;
	// 	}
	if (animSlot == RailController::kSlotCount)
		return false;

	const VehicleDeathHint* pVehicleDeathHint = VehicleDeathHintMgr::FindVehicleDeathHint(pVehicle,
																						  pRailController->GetSpline())
													.ToProcess();
	const StringId64 preDeathHintAnimId = pVehicleDeathHint
											  ? pRailController->GetAnimationName(RailController::kAnimFacingFront,
																				  RailController::kAnimStatePreDeathHint,
																				  RailController::kSlotDriver)
											  : INVALID_STRING_ID_64;

	// look up death animation
	StringId64 animId = pVehicleDeathHint ? preDeathHintAnimId
										  : pRailController->GetAnimationName(RailController::kAnimFacingFront,
																			  RailController::kAnimStateNpcDead,
																			  animSlot);
	if (animId == INVALID_STRING_ID_64)
		return false;

	const BoundFrame apRef = pRailController->GetNpcDeathAnimBoundFrame();
	const StringId64 apChannelId = pRailController->GetNpcDeathAnimApChannelId();
	pNpc->SetDeathAnimApChannel(apChannelId);

	DieSpecial(animId, &apRef, apChannelId, pRailController->GetDeathAnimationBlendTime());

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::DieOnHorse()
{
	Npc* pNpc = Npc::FromProcess(GetCharacter());
	if (!pNpc)
		return;

	const IAiRideHorseController* pRideHorse = pNpc->GetAnimationControllers()->GetRideHorseController();
	GAMEPLAY_ASSERT(pRideHorse);

	const Horse* pHorse = pRideHorse->GetHorse();
	GAMEPLAY_ASSERT(pHorse);

	if (IGestureController* pGestureController = pNpc->GetGestureController())
	{
		pGestureController->ClearNonBaseGestures();
	}

	HorseDefines::RiderPosition riderPos = pRideHorse->GetRiderPos();
	const BoundFrame bf = pHorse->GetRiderAttachBoundFrame(riderPos);

	PlayAnimParams params;
	params.m_fromHorseDeath = true;
	params.m_frame		 = bf;
	params.m_pAnimAction = &m_fullBodyAnimAction;
	PlayDeathAnim(params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::DieOnBoatHack(const HitDescription& hitDesc)
{
	NavCharacter* pNavChar = GetCharacter();
	if (!pNavChar)
	{
		return false;
	}

	const StringId64 npcId	   = pNavChar->GetUserId();
	StringId64 hackDeathAnimId = INVALID_STRING_ID_64;

	switch (npcId.GetValue())
	{
	case SID_VAL("npc-boat-wave-2-1"):
		hackDeathAnimId = SID("thug-death-stand-explosion-high-back-c");
		break;
	case SID_VAL("npc-boat-wave-2-2"):
		hackDeathAnimId = SID("thug-death-stand-explosion-high-left");
		break;

	default:
		return false;
	}

	if (hackDeathAnimId == INVALID_STRING_ID_64)
	{
		return false;
	}

	bool isExplosionDeath = false;
	switch (hitDesc.m_sourceType)
	{
	case DC::kHitReactionSourceTypeExplosionStrong:
	case DC::kHitReactionSourceTypeExplosionWeak:
	case DC::kHitReactionSourceTypeExplosionFlinch:
		isExplosionDeath = true;
		break;
	}

	if (!isExplosionDeath)
	{
		return false;
	}

	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const ArtItemAnim* pAnim  = pAnimControl ? pAnimControl->LookupAnim(hackDeathAnimId).ToArtItem() : nullptr;

	if (!pAnim)
	{
		return false;
	}

	const Locator& alignPs	= pNavChar->GetLocatorPs();
	const SkeletonId skelId = pNavChar->GetSkeletonId();
	Locator apRefPs			= alignPs;

	if (!FindApReferenceFromAlign(skelId, pAnim, alignPs, &apRefPs, 0.0f, false))
	{
		return false;
	}

	BoundFrame apRef = pNavChar->GetBoundFrame();
	apRef.SetLocatorPs(apRefPs);

	DieSpecial(hackDeathAnimId, &apRef, SID("apReference"), 0.2f);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsBeingShoved() const
{
	return !m_shoveAnimAction.IsDone();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::Stun(float stunTime, DC::HitReactionSourceType hitReactionSourceType)
{
	NavCharacter* pNavChar	   = GetCharacter();
	AnimControl* pAnimControl  = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	if (pNavChar->IsKindOf(g_type_Horse))
		return false;

	if (const Npc* pNpc = Npc::FromProcess(pNavChar))
	{
		if (!pNpc->GetArchetype().AllowStun())
			return false;

		if (!pNpc->IsStunEnabled())
			return false;
	}

	if (pNavChar->IsInScriptedAnimationState())
	{
		if (const AnimationControllers* pAnimationControllers = pNavChar->GetAnimationControllers())
		{
			if (const AiScriptController* pScriptController = pAnimationControllers
																  ->GetScriptController(kNpcScriptFullBodyController))
			{
				const SsAnimateParams& animParams = pScriptController->GetParams();
				if (!animParams.m_allowGetAttacked)
				{
					return false;
				}
			}
		}
	}

	AiLogAnim(pNavChar, "AiHitController::Stun [stunTime: %0.3f sec] [source: 0x%x]\n", stunTime, hitReactionSourceType);

	m_stunAnimEndTime = pNavChar->GetCurTime() + Seconds(stunTime);

	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	m_lastFullBodySourceType = hitReactionSourceType;

	m_interruptNavigation = true;
	m_interruptSkills	  = true;

	FadeToStateParams params;
	params.m_animFadeTime = 0.2f;

	m_fullBodyAnimAction.FadeToState(pAnimControl,
									 SID("s_stun-intro"),
									 params,
									 AnimAction::kFinishOnNonTransitionalStateReached);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::GoLegless(bool leftLeg, const AttackInfo* pAttackInfo)
{
	const Infected* pInfected = Infected::FromProcess(GetCharacter());
	if (!pInfected)
		return false;

	const DC::AiInfectedLeglessParams* pLeglessParams = pInfected->GetParams()->m_leglessParams;
	if (!pLeglessParams)
		return false;

	BoundFrame bf = pInfected->GetBoundFrame();

	m_leglessHitReaction = DC::AiInfectedLeglessHitReaction();

	if (pAttackInfo && pAttackInfo->m_type == DC::kAttackTypeExplosion)
	{
		const Quat rot = pInfected->GetRotation();
		const Vector fw = GetLocalZ(rot);
		const Vector lt = GetLocalX(rot);
		const ExplosionAttackInfo* pExplosionAttackInfo = static_cast<const ExplosionAttackInfo*>(pAttackInfo);
		const Vector toExplosion = SafeNormalize(pExplosionAttackInfo->m_source - pInfected->GetTranslation(), fw);
		const F32 dotFw = Dot(toExplosion, fw);
		const F32 dotLt = Dot(toExplosion, lt);

		m_leglessHitReaction =
			Abs(dotFw) > Abs(dotLt)
				? dotFw > 0.0f
					? pLeglessParams->m_hitReactionFromExplosionFw
					: pLeglessParams->m_hitReactionFromExplosionBw
				: dotLt > 0.0f
					? pLeglessParams->m_hitReactionFromExplosionLt
					: pLeglessParams->m_hitReactionFromExplosionRt;


		if (!m_leglessHitReaction.m_anim)
		{
			if (FALSE_IN_FINAL_BUILD(true))
			{
				const char* dirStr =
					Abs(dotFw) > Abs(dotLt)
						? dotFw > 0.0f
							? "front"
							: "back"
						: dotLt > 0.0f
							? "left"
							: "right";

				const StringBuilder<128> sb("WARNING:\nNo explosion legless hit reaction from %s", dirStr);

				g_prim.Draw(DebugString(pInfected->GetTranslation(), sb.c_str(), kColorRed, 0.5f), Seconds(3.0f));
			}

			return false;
		}

		bf.SetRotation(QuatFromXZDir(toExplosion));
	}
	else
	{
		const bool isKneeStun = IsStunKneeReactionPlaying();

		m_leglessHitReaction =
			isKneeStun
				? leftLeg
					? pLeglessParams->m_hitReactionFromKneeStunLt
					: pLeglessParams->m_hitReactionFromKneeStunRt
				: leftLeg
					? pLeglessParams->m_hitReactionLt
					: pLeglessParams->m_hitReactionLt;

		if (!m_leglessHitReaction.m_anim)
		{
			if (FALSE_IN_FINAL_BUILD(true))
			{
				const char* kneeStunStr = isKneeStun ? "knee stun " : "";
				const char* legSideStr = leftLeg ? "left" : "right";

				const StringBuilder<128> sb("WARNING:\nNo %slegless hit reaction on %s leg", kneeStunStr, legSideStr);

				g_prim.Draw(DebugString(pInfected->GetTranslation(), sb.c_str(), kColorRed, 0.5f), Seconds(3.0f));
			}

			return false;
		}
	}

	AI_ASSERT(m_leglessHitReaction.m_anim);

	m_interruptNavigation = true;
	m_interruptSkills = true;

	PlayAnimParams params;
	params.m_pAnimAction = &m_fullBodyAnimAction;
	params.m_animId		 = m_leglessHitReaction.m_anim;
	params.m_startPhase	 = 0.0f;
	params.m_exitPhase	 = 0.9f;
	params.m_frame		 = bf;
	params.m_mirror		 = false;

	m_lastFullBodySourceType = DC::kHitReactionSourceTypeInfectedLegDismemberment;

	return PlayHitAnim(params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::Shove(Character* pShover, Point_arg originWs, Vector_arg motionDirWs)
{
	NavCharacter* pNavChar	   = GetCharacter();
	AnimControl* pAnimControl  = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	BoundFrame evadeBf;

	m_allowNpcCollision = false;

	if (pShover->IsKindOf(g_type_Npc))
	{
		AI_ASSERT(false); // THis should be handled in HandleNpcNpcCollision
		return false;
	}
	else
	{
		Vector shoverRight = GetLocalX(pShover->GetRotation());

		StringId64 animId = SID("npc-run-push-front");

		// if we are facing away use different anim
		if (Dot(GetLocalZ(pNavChar->GetRotation()), motionDirWs) > 0.0f)
		{
			animId = SID("npc-bump-high-back-l");
		}

		const bool mirror = Dot(shoverRight, pNavChar->GetTranslation() - originWs) < 0.0f;

		AI::SetPluggableAnim(pNavChar, animId, mirror);

		evadeBf = pShover->GetBoundFrame();
		evadeBf.SetRotation(QuatFromXZDir(motionDirWs));
		m_allowNpcCollision = true;
	}

	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	FadeToStateParams params;
	params.m_apRef		  = evadeBf;
	params.m_apRefValid	  = true;
	params.m_animFadeTime = 0.2f;

	m_shoveAnimAction.FadeToState(pAnimControl, SID("s_shoved-state"), params);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
IAiHitController* CreateAiHitController()
{
	return NDI_NEW AiHitController;
}

//////////////////////////////////////////////////////////////////////////

bool IAiHitController::CheckForNpcCollision(NavCharacter* pNavChar)
{
	bool result = false;
	if (pNavChar)
	{
		Point prevPos = pNavChar->GetParentSpace().TransformPoint(pNavChar->GetLastTranslationPs());
		Point curPos  = pNavChar->GetTranslation();
		Point newPos  = curPos + (curPos - prevPos);

		Sphere sphere(Lerp(newPos, curPos, 0.5f), Dist(newPos, curPos) + pNavChar->GetMaximumNavAdjustRadius());
		Vector dir = VectorXz(newPos - curPos);
		// 		g_prim.Draw(DebugSphere(sphere, kColorWhite));
		// 		g_prim.Draw(DebugCircle(curPos, Vector(kUnitYAxis), pNavChar->GetMaximumNavAdjustRadius()));
		// 		g_prim.Draw(DebugCircle(newPos, Vector(kUnitYAxis), pNavChar->GetMaximumNavAdjustRadius()));
		const U32 kMaxNpcs = 32;
		MutableNdGameObjectHandle npcs[kMaxNpcs];
		U32F numNpcs = AiPotentialTargets::Get().FindNearbyNpcsWs(sphere, npcs, kMaxNpcs);
		MutableNdGameObjectHandle bumpedCharacterHandle = nullptr;

		for (U32F iNpc = 0; iNpc < numNpcs; ++iNpc)
		{
			if (npcs[iNpc].GetProcessId() == pNavChar->GetProcessId())
				continue;

			Npc* pNpc = Npc::FromProcess(npcs[iNpc].ToMutableProcess());

			g_prim.Draw(DebugSphere(pNpc->GetTranslation(), pNpc->GetMaximumNavAdjustRadius()));
			Scalar distToNpc = DistPointSegment(pNpc->GetTranslation(), curPos, newPos);
			if (Dot(dir, pNpc->GetTranslation() - curPos) > Scalar(kZero)
				&& distToNpc < pNavChar->GetMaximumNavAdjustRadius() + pNpc->GetMaximumNavAdjustRadius())
			{
				// we collided with this npc
				bumpedCharacterHandle = pNpc;
				break;
			}
		}

		if (bumpedCharacterHandle.HandleValid())
		{
			Npc* pBumpedNpc = Npc::FromProcess(bumpedCharacterHandle.ToMutableProcess());
			if (pBumpedNpc && !pBumpedNpc->IsBeingShoved())
			{
				if (true || pBumpedNpc->CanBeShoved(pNavChar))
				{
					AiHitController::HandleNpcNpcCollision(pBumpedNpc, pNavChar);
					result = true;
				}
				else
				{
					MsgAi("CheckForNpcCollision: %s running into %s but CanBeShoved returned false\n",
						  pNavChar->GetName(),
						  pBumpedNpc->GetName());
				}
			}
		}
	}
	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::PlayNpcStumble(NavCharacter* pCatcher, StringId64 anim, const BoundFrame& ap)
{
	AiHitController* pCatcherController = static_cast<AiHitController*>(pCatcher->GetAnimationControllers()
																			->GetHitController());

	// 	if (IAiMeleeActionController* pMeleeActionController = pCatcher->GetAnimationControllers()->GetSyncActionController()->GetMeleeActionController())
	// 	{
	// 		pMeleeActionController->StopMeleeAction();
	// 	}

	{
		AI::SetPluggableAnim(pCatcher, anim);

		AnimControl* pAnimControl  = pCatcher->GetAnimControl();
		AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

		pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

		FadeToStateParams params;
		params.m_apRef		  = ap;
		params.m_apRefValid	  = true;
		params.m_animFadeTime = 0.2f;

		pCatcherController->m_shoveAnimAction.FadeToState(pAnimControl, SID("s_shoved-state"), params);
		pCatcherController->m_allowNpcCollision = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::HandleNpcNpcCollision(NavCharacter* pCatcher, NavCharacter* pMover)
{
	const Vector moverToCatcher = pCatcher->GetTranslation() - pMover->GetTranslation();
	const Vector catcherFacing	= GetLocalZ(pCatcher->GetRotation());
	const Vector moverFacing	= GetLocalZ(pMover->GetRotation());

	StringId64 catcherAnim = INVALID_STRING_ID_64;
	BoundFrame catcherAp;
	StringId64 moverAnim = INVALID_STRING_ID_64;
	BoundFrame moverAp;

	// Determine catch anims
	if (Dot(moverToCatcher, catcherFacing) < 0.0f)
	{
		if (Dot(catcherFacing, moverFacing) > 0.0f)
		{
			catcherAnim = SID("thug-fist-bump-high-back^catch-front-b");
			moverAnim	= SID("thug-fist-bump-high-back^catch-front-a");

			catcherAp = pCatcher->GetBoundFrame();
			catcherAp.SetRotation(QuatFromXZDir(-moverToCatcher));
		}
		else
		{
			catcherAnim = SID("thug-fist-bump-high-front^catch-front-b");
			moverAnim	= SID("thug-fist-bump-high-front^catch-front-a");
			catcherAp	= pCatcher->GetBoundFrame();
			catcherAp.SetRotation(QuatFromXZDir(-moverToCatcher));
		}
		moverAp = catcherAp;
	}
	else
	{
		if (Dot(catcherFacing, moverFacing) > 0.0f)
		{
			catcherAnim = SID("thug-fist-bump-high-front^catch-back-b");
			moverAnim	= SID("thug-fist-bump-high-front^catch-back-a");

			catcherAp = pCatcher->GetBoundFrame();
			catcherAp.SetRotation(QuatFromXZDir(-moverToCatcher));
		}
		else
		{
			catcherAnim = SID("thug-fist-bump-high-back^catch-back-b");
			moverAnim	= SID("thug-fist-bump-high-back^catch-back-a");
			catcherAp	= pCatcher->GetBoundFrame();
			catcherAp.SetRotation(QuatFromXZDir(-moverToCatcher));
		}
		moverAp = catcherAp;
	}

	AiHitController::PlayNpcStumble(pCatcher, catcherAnim, catcherAp);
	AiHitController::PlayNpcStumble(pMover, moverAnim, moverAp);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::DebugDraw(ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	if (!g_aiGameOptions.m_hitReaction.m_displayHitController)
		return;

	const NavCharacter* pNavChar = GetCharacter();
	const Point aimOriginWs		 = pNavChar->GetAimOriginWs();

	const StringId64 bucketId = GetCurrentActiveBucket();

	StringBuilder<256> desc;
	desc.clear();

	Color clr = kColorYellow;

	if (bucketId != INVALID_STRING_ID_64)
	{
		desc.append_format("%s", DevKitOnly_StringIdToString(bucketId));
	}
	else
	{
		clr = !m_fullBodyAnimAction.IsDone() ? kColorWhite : kColorGrayTrans;
		desc.append_format("default");
	}

	const AnimTypeFilter animTypeFilter = GetAnimTypeFilter(nullptr);
	switch (animTypeFilter)
	{
	case AnimTypeFilter::kAdditiveOnly:
		desc.append_format(" (add-only)");
		break;
	case AnimTypeFilter::kFullBodyOnly:
		desc.append_format(" (full-body-only)");
		break;
	}

	g_prim.Draw(DebugString(aimOriginWs, desc.c_str(), clr));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::OverrideNextHitDirectionWs(Point_arg hitDirTargetWs)
{
	NavCharacter* pNavChar = GetCharacter();
	if (!pNavChar)
		return;

	const Locator& parentSpace = pNavChar->GetParentSpace();
	m_hitDirOverrideValid	   = true;
	m_hitDirOverrideTargetPs   = parentSpace.UntransformPoint(hitDirTargetWs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::KnockDown()
{
	NavCharacter* pNavChar = GetCharacter();
	if (!pNavChar)
		return;

	BoundFrame boundFrame	   = pNavChar->GetBoundFrame();
	AnimControl* pAnimControl  = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	FadeToStateParams params;
	params.m_apRef		  = boundFrame;
	params.m_apRefValid	  = true;
	params.m_animFadeTime = 0.2f;

	m_knockDownAnimAction.FadeToState(pAnimControl,
									  SID("s_knocked-down-intro"),
									  params,
									  AnimAction::kFinishOnNonTransitionalStateReached);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::CanInitiateMeleeAttack() const
{
	// we check this function to see whether npc can initiate melee attack.
	if (!IsBusy())
		return true;

	if (m_pLastFullBodyHitEntry && (m_pLastFullBodyHitEntry->m_flags & DC::kHitReactionFlagsAllowInitiateMelee))
		return true;

	if (m_pLastFullBodyHitEntry == nullptr)
		return true;

	switch (m_lastFullBodySourceType)
	{
	case DC::kHitReactionSourceTypeMeleeDanger:
		return true;

	case DC::kHitReactionSourceTypeInfectedLegDismemberment:
		if (!m_fullBodyAnimAction.IsDone())
		{
			const Infected* pInfected = Infected::FromProcess(GetCharacter());
			if (!pInfected)
				break;

			const F32 phase = m_fullBodyAnimAction.GetAnimPhase(GetCharacter()->GetAnimControl());
			if (phase >= m_leglessHitReaction.m_meleePhase)
				return true;
		}
		break;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::NetHitReaction(StringId64 animListId,
									 StringId64 bucketId,
									 U32 animIndex,
									 float startPhase,
									 F32 playbackRate,
									 TimeFrame startTime,
									 const BoundFrame& boundFrame)
{
	const Npc* pNpc = Npc::FromProcess(GetCharacter());
	if (!pNpc)
		return;

	const DC::HitReactionEntryList* pHitList = GetHitAnimationList(animListId, bucketId);
	AI_ASSERT(pHitList);
	if (!pHitList)
		return;

	if (animIndex >= pHitList->m_count)
		return;

	const ProcessMeleeAction* pAction = pNpc->GetCurrentMeleeAction();
	if ((!IsBusy() || startTime > m_lastNetReactTime) && (!pAction || startTime <= pAction->GetTimeSinceStart()))
	{
		m_lastNetReactTime		= startTime;
		m_interruptNavigation	= true;
		m_pLastSelectedHitEntry = &pHitList->m_array[animIndex];
		if (m_pLastSelectedHitEntry->m_animType == DC::kHitReactionAnimTypeFullBody)
			m_pLastFullBodyHitEntry = m_pLastSelectedHitEntry;
		const float exitPhase = m_pLastFullBodyHitEntry
									? Max(m_pLastFullBodyHitEntry->m_exitPhase, m_lastFullBodyFadeOutPhase)
									: 1.0f;
		const bool mirror = m_pLastSelectedHitEntry->m_flags & DC::kHitReactionFlagsMirror;

		PlayAnimParams params;
		params.m_pAnimAction = &m_fullBodyAnimAction;
		params.m_animId		 = m_pLastSelectedHitEntry->m_animId;
		params.m_startPhase	 = startPhase;
		params.m_exitPhase	 = exitPhase;
		params.m_frame		 = boundFrame;
		params.m_mirror		 = mirror;
		params.m_handoffType = HandoffType::kNone;

		switch (m_pLastSelectedHitEntry->m_exitMode)
		{
		case DC::kHitReactionExitModeMoving:
			params.m_handoffType = HandoffType::kMoving;
			params.m_dtGroupId	 = SID("hit-exit^move");
			break;

		case DC::kHitReactionExitModeIdle:
			params.m_handoffType = HandoffType::kIdle;
			params.m_dtGroupId	 = SID("hit-exit^idle");
			break;
		}

		if (!AllHitAnimsDisabled(DC::kHitReactionSourceTypeMax))
			PlayHitAnim(params);

		SetLastHitTime(pNpc->GetClock()->GetCurTime());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::NetDeathReaction(StringId64 animListId,
									   StringId64 bucketId,
									   U32 animIndex,
									   F32 exitPhase,
									   const BoundFrame& boundFrame,
									   bool mirror)
{
	if (IsBusy())
		return;

	const Npc* pNpc = Npc::FromProcess(GetCharacter());
	if (!pNpc)
		return;

	const DC::HitReactionEntryList* pHitList = GetHitAnimationList(animListId, bucketId);
	AI_ASSERT(pHitList);
	if (!pHitList)
		return;

	if (animIndex >= pHitList->m_count)
		return;

	m_interruptNavigation	= true;
	m_pLastSelectedHitEntry = &pHitList->m_array[animIndex];
	if (m_pLastSelectedHitEntry->m_animType == DC::kHitReactionAnimTypeFullBody)
		m_pLastFullBodyHitEntry = m_pLastSelectedHitEntry;

	PlayAnimParams params;
	params.m_pAnimAction = &m_fullBodyAnimAction;
	params.m_animId		 = m_pLastSelectedHitEntry->m_animId;
	params.m_exitPhase	 = exitPhase;
	params.m_frame		 = boundFrame;
	params.m_mirror		 = mirror;

	PlayDeathAnim(params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
TimeFrame AiHitController::GetLastNetReactTime()
{
	return m_lastNetReactTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiHitController::GetLastMeleeHitReactionAnim()
{
	return m_lastHitReactionMeleeAnim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiHitController::GetCurrentActiveBucket() const
{
	const NavCharacter* pNavChar	= GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();

	if (m_fullBodyAnimAction.IsDone() || !m_fullBodyAnimAction.IsValid() || !m_pLastFullBodyHitEntry)
		return INVALID_STRING_ID_64;

	if (m_pLastFullBodyHitEntry->m_transitionArray && (m_pLastFullBodyHitEntry->m_transitionCount > 0))
	{
		const float phase = m_fullBodyAnimAction.GetAnimPhase(pAnimControl);

		for (U32F i = 0; i < m_pLastFullBodyHitEntry->m_transitionCount; ++i)
		{
			const DC::HitReactionBucketTransition* pTrans = &m_pLastFullBodyHitEntry->m_transitionArray[i];
			const float tphase = pTrans->m_endPhase;

			if (phase <= tphase)
			{
				if (pTrans->m_bucketId != SID("none"))
				{
					return pTrans->m_bucketId;
				}
				else
				{
					return m_lastHitReactionBucket;
				}
			}
		}
	}

	return m_lastHitReactionBucket;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiHitController::GetDistanceErrorForAnim(const SkeletonId skelId,
											   const ArtItemAnim* pAnim,
											   float phase,
											   const Locator rootOs,
											   const Locator leftShOs,
											   const Locator rightShOs,
											   bool debugDraw /*= false*/) const
{
	if (!pAnim)
		return 0.0f;

	Locator animRootOs, animLeftShOs, animRightShOs;
	if (!EvaluateChannelInAnim(skelId, pAnim, SID("root"), phase, &animRootOs))
		return -1.0f;
	if (!EvaluateChannelInAnim(skelId, pAnim, SID("lShoulder"), phase, &animLeftShOs))
		return -1.0f;
	if (!EvaluateChannelInAnim(skelId, pAnim, SID("rShoulder"), phase, &animRightShOs))
		return -1.0f;

	const float rootDistErr = Dist(rootOs.Pos(), animRootOs.Pos());
	const float lsDistErr	= Dist(leftShOs.Pos(), animLeftShOs.Pos());
	const float rsDistErr	= Dist(rightShOs.Pos(), animRightShOs.Pos());

	const float maxDistErr = Max(rootDistErr, Max(lsDistErr, rsDistErr));

	if (debugDraw)
	{
		const Locator alignWs = GetCharacter()->GetLocator();
		g_prim.Draw(DebugCoordAxesLabeled(alignWs.TransformLocator(animRootOs),
										  StringBuilder<128>("%0.3f", rootDistErr).c_str(),
										  0.1f,
										  PrimAttrib(),
										  2.0f,
										  kColorWhite,
										  0.5f),
					Seconds(3.0f));
		g_prim.Draw(DebugCoordAxesLabeled(alignWs.TransformLocator(animLeftShOs),
										  StringBuilder<128>("%0.3f", lsDistErr).c_str(),
										  0.1f,
										  PrimAttrib(),
										  2.0f,
										  kColorWhite,
										  0.5f),
					Seconds(3.0f));
		g_prim.Draw(DebugCoordAxesLabeled(alignWs.TransformLocator(animRightShOs),
										  StringBuilder<128>("%0.3f", rsDistErr).c_str(),
										  0.1f,
										  PrimAttrib(),
										  2.0f,
										  kColorWhite,
										  0.5f),
					Seconds(3.0f));
	}

	return maxDistErr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::VisualizeHitReaction(const HitDescription* pHitDesc,
										   const PotentialHitAnim* pSelectedAnim,
										   float startPhase /* = -1.0f */,
										   float playbackRate /* = -1.0f */) const
{
	STRIP_IN_FINAL_BUILD;

	if (!pHitDesc)
		return;

	const NavCharacter* pNavChar = GetCharacter();
	const float curSpeed		 = Length(pNavChar->GetVelocityPs());

	const AnimTypeFilter animTypeFilter		   = GetAnimTypeFilter(pHitDesc);
	const DC::HitReactionDirectionMask dirMask = GetHitDirection(*pHitDesc);
	const DC::HitReactionSourceType attackType = pHitDesc->m_sourceType;
	const DC::HitReactionStateMask npcState	   = pHitDesc->m_npcState;

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	char* pBuf = NDI_NEW char[1024];
	char* pCur = pBuf;

	const StringId64 bucketId = GetCurrentActiveBucket();

	const char* pAnimTypeFilterStr = "";
	switch (animTypeFilter)
	{
	case AnimTypeFilter::kAdditiveOnly:
		pAnimTypeFilterStr = " (add-only)";
		break;
	case AnimTypeFilter::kFullBodyOnly:
		pAnimTypeFilterStr = " (full-body-only)";
		break;
	}

	const Npc* pNpc		   = Npc::FromProcess(pNavChar);
	const bool stunAllowed = pNpc ? pNpc->IsStunEnabled() : true;

	pCur += sprintf(pCur,
					"[%s%s] %s @ %0.1fm/s\n",
					(bucketId != INVALID_STRING_ID_64) ? DevKitOnly_StringIdToString(bucketId) : "default",
					pAnimTypeFilterStr,
					GetNpcStateName(npcState),
					curSpeed);
	pCur += sprintf(pCur,
					"%s -> %s%s @ %0.1fm (player %0.1fm) = %d dmg %s",
					GetAttackTypeName(attackType),
					DevKitOnly_StringIdToString(GetHitJointId(*pHitDesc)),
					pHitDesc->m_shotForCenterMass ? " (centerMass)" : "",
					pHitDesc->m_distToTarget,
					pHitDesc->m_distToPlayer,
					pHitDesc->m_damage,
					pHitDesc->m_stun ? " (stun) " : (stunAllowed ? "" : " (stun-disabled) "));
	if (pHitDesc->m_stoppingPower > 0.0f)
	{
		pCur += sprintf(pCur, " + %0.2f stp", pHitDesc->m_stoppingPower);
	}

	pCur += sprintf(pCur, "%s\n", GetDirectionMaskName(dirMask));

	if (!m_fullBodyAnimAction.IsDone() && m_fullBodyAnimAction.IsValid() && m_pLastFullBodyHitEntry)
	{
		const AnimStateInstance* pInst = m_fullBodyAnimAction.GetTransitionDestInstance(GetCharacter()->GetAnimControl());
		const ArtItemAnim* pPhaseAnim = pInst ? pInst->GetPhaseAnimArtItem().ToArtItem() : nullptr;
		if (pPhaseAnim)
		{
			pCur += sprintf(pCur, "%s - %0.2f\n", pPhaseAnim->GetName(), pInst->Phase());
		}
	}

	pCur += sprintf(pCur, "------------\n");

	if (pSelectedAnim)
	{
		const float startPhaseToUse	  = (startPhase >= 0.0f) ? startPhase : pSelectedAnim->m_startPhase;
		const float playbackRateToUse = (playbackRate >= 0.0f) ? playbackRate : 1.0f;

		pCur += sprintf(pCur,
						"%s @ %0.3f (speed: %0.2fx)\n",
						pSelectedAnim->m_pAnim ? pSelectedAnim->m_pAnim->GetName() : "<no anim>",
						startPhaseToUse,
						playbackRateToUse);
		pCur += sprintf(pCur, "Blend Time: %0.1f (Err %0.3f)\n", pSelectedAnim->m_blendTime, pSelectedAnim->m_distErr);
	}
	else
	{
		pCur += sprintf(pCur, "NO MATCH\n");
	}

	if (g_aiGameOptions.m_hitReaction.m_hitReactionLogMode & AiGameOptions::kLogModeVisualize)
	{
		const BoundFrame& frame = pNavChar->GetBoundFrame();
		// 	const Locator parentSpace = pNavChar->GetParentSpace();

		const Sphere bSphere	   = pNavChar->GetBoundingSphere();
		const Point rawImpactPosWs = pHitDesc->m_impactPointWs;
		const Vector toImpactPosWs = LimitVectorLength(rawImpactPosWs - bSphere.GetCenter(), 0.0f, bSphere.GetRadius());
		const Point impactPosWs	   = bSphere.GetCenter() + toImpactPosWs;
		const Point offsetPosWs	   = impactPosWs
								  + 3.0f
										* SafeNormalize(Vector(RandomFloatRange(-2.0f, 2.0f),
															   RandomFloatRange(0.25f, 0.5f),
															   RandomFloatRange(-2.0f, 2.0f)),
														kUnitZAxis);

		// 	const Point impactPosPs = parentSpace.UntransformPoint(impactPosWs);
		// 	const Point offsetPosPs = parentSpace.UntransformPoint(offsetPosWs);

		const Color clr = Color(urand() | 0xFF000000);
		g_prim.Draw(DebugLine(impactPosWs, offsetPosWs, clr), Seconds(3.0f), &frame);

		TextPrinterParentSpace tp = TextPrinterParentSpace(frame, Seconds(3.0f));

		tp.Start(offsetPosWs, 1024);
		tp.PrintF(clr, pBuf);
	}

	if (g_aiGameOptions.m_hitReaction.m_hitReactionLogMode & AiGameOptions::kLogModeTTY)
	{
		MsgAnim("---------- Visualize Hit Reaction ----------\n");
		MsgAnim(pBuf);
		MsgAnim("-------- End Visualize Hit Reaction --------\n");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point AiHitController::GetHitAnimApRefTranslationWs(const PotentialHitAnim* pPotential, Quat_arg apRefRotWs) const
{
	const NavCharacter* pNavChar  = GetCharacter();
	const NavControl* pNavControl = GetNavControl();
	const SkeletonId skelId		  = pNavChar->GetSkeletonId();
	const Locator& ps	   = pNavChar->GetParentSpace();
	const Locator alignWs  = pNavChar->GetLocator();
	const Locator& alignPs = pNavChar->GetLocatorPs();

	Locator inputAlignPs = alignPs;

	const Locator inputAlignWs = ps.TransformLocator(inputAlignPs);
	const bool mirror = pPotential->m_mirror;

	if (!pPotential->m_pAnim)
	{
		return inputAlignWs.Pos();
	}

	const Locator rotApRefWs = Locator(inputAlignWs.Pos(), apRefRotWs);
	Locator initialRotatedAlignWs = rotApRefWs;
	FindAlignFromApReference(skelId,
							 pPotential->m_pAnim,
							 pPotential->m_startPhase,
							 rotApRefWs,
							 SID("apReference"),
							 &initialRotatedAlignWs,
							 mirror);

	Locator alignLs = Locator(kIdentity);
	if (!FindAlignFromApReference(skelId,
								  pPotential->m_pAnim,
								  pPotential->m_startPhase,
								  Locator(kIdentity),
								  SID("apReference"),
								  &alignLs,
								  mirror))
	{
		return inputAlignWs.Pos();
	}

	const Locator invAlignLs = Inverse(alignLs);
	const Locator rotatedInputAlignWs = Locator(inputAlignWs.Pos(), initialRotatedAlignWs.Rot());
	const Locator apRefWs = rotatedInputAlignWs.TransformLocator(invAlignLs);

	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_visualizeHitReactions
							 && (g_aiGameOptions.m_hitReaction.m_hitReactionLogMode & AiGameOptions::kLogModeVisualize)))
	{
		const BoundFrame& frame = pNavChar->GetBoundFrame();

		g_prim.Draw(DebugCoordAxesLabeled(apRefWs,
										  StringBuilder<256>("%s @ %0.1f",
															 pPotential->m_pAnim->GetName(),
															 pPotential->m_startPhase)
											  .c_str(),
										  0.3f,
										  PrimAttrib(),
										  4.0f),
					Seconds(3.0f),
					&frame);

		g_prim.Draw(DebugCoordAxesLabeled(rotatedInputAlignWs, "rotatedInputAlignWs", 0.5f), Seconds(3.0f), &frame);
	}

	return apRefWs.Pos();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Quat AiHitController::GetHitAnimApRefRotationWs(const PotentialHitAnim* pHitAnim, const HitDescription* pHitDesc) const
{
	const NavCharacter* pNavChar = GetCharacter();
	const Quat charRotWs		 = pNavChar->GetRotation();
	const Locator& ps = pNavChar->GetParentSpace();

	if (!pHitAnim || !pHitAnim->m_pDcEntry)
		return charRotWs;

	if (0 != (pHitAnim->m_pDcEntry->m_flags & DC::kHitReactionFlagsDontOrientAlign))
	{
		const SkeletonId skelId = pNavChar->GetSkeletonId();
		const Locator locWs		= pNavChar->GetLocator();
		Locator apRefWs			= locWs;
		const bool mirror		= pHitAnim->m_mirror;

		if (FindApReferenceFromAlign(skelId, pHitAnim->m_pAnim, locWs, &apRefWs, pHitAnim->m_startPhase, mirror))
		{
			return apRefWs.Rot();
		}
		else
		{
			return charRotWs;
		}
	}

	const Point overridePosWs = ps.TransformPoint(m_hitDirOverrideTargetPs);
	const Vector overrideDirWs = overridePosWs - pNavChar->GetTranslation();

	const Vector hitDirWs = m_hitDirOverrideValid ? overrideDirWs : pHitDesc->m_directionWs;
	const Vector flattenedHitVecWs = ProjectOntoPlane(hitDirWs, kUnitYAxis);
	const Vector flattenedHitDirWs = SafeNormalize(flattenedHitVecWs, kUnitXAxis);

	// By default we create the apReference in the opposite direction of the hit direction.
	const bool invertAp			= pHitAnim->m_pDcEntry->m_flags & DC::kHitReactionFlagsInvertAp;
	const Vector apForwardDirWs = invertAp ? flattenedHitDirWs : -flattenedHitDirWs;

	const Quat newOrientWs = QuatFromLookAt(apForwardDirWs, kUnitYAxis);
	ASSERT(IsFinite(newOrientWs));

	return newOrientWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::StateMaskMatches(const DC::HitReactionStateMask npcState,
									   const DC::HitReactionEntry* pDcEntry) const
{
	if (!pDcEntry)
		return false;

	bool matches = false;

	if ((pDcEntry->m_flags & DC::kHitReactionFlagsExactStateMatch) != 0)
	{
		matches = npcState == pDcEntry->m_stateMask;
	}
	else
	{
		const DC::HitReactionStateMask armorStatesRequested = npcState & kArmorMask;
		const DC::HitReactionStateMask npcStateNoArmor		= npcState & ~kArmorMask;
		const DC::HitReactionStateMask entryStateNoArmor	= pDcEntry->m_stateMask & ~kArmorMask;
		const bool armorMatches = ((armorStatesRequested & pDcEntry->m_stateMask) == armorStatesRequested)
								  && ((npcStateNoArmor & entryStateNoArmor) != 0);

		const DC::HitReactionStateMask limbStateRequested = npcState & kLimbMask;
		const bool limbMatches = (!limbStateRequested) || (limbStateRequested & pDcEntry->m_stateMask);

		const DC::HitReactionStateMask meleeHrRequested = npcState & kMeleeHrFilterMask;
		const bool meleeHrFilterMatches = (!meleeHrRequested) || (meleeHrRequested & pDcEntry->m_stateMask);

		matches = armorMatches && limbMatches && meleeHrFilterMatches;
	}

	return matches;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::HitJointIdMatches(const StringId64 hitJointId, const DC::HitReactionEntry* pDcEntry) const
{
	if (!pDcEntry)
		return false;

	if (pDcEntry->m_hitAreaCount == 0)
		return true;

	for (U32F i = 0; i < pDcEntry->m_hitAreaCount; ++i)
	{
		if (pDcEntry->m_hitAreaArray[i] == hitJointId)
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::HitEntryValidForCenterOfMass(const DC::HitReactionEntry* pDcEntry) const
{
	if (!pDcEntry)
		return false;

	for (U32F i = 0; i < pDcEntry->m_hitAreaCount; ++i)
	{
		if (pDcEntry->m_hitAreaArray[i] == SID("centerMass"))
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::DamageRangeMatches(const HitDescription* pHitDesc, const DC::HitReactionEntry* pDcEntry) const
{
	if (!pDcEntry || !pHitDesc)
		return false;

	if (pDcEntry->m_damageRange)
	{
		if (pDcEntry->m_damageRange->m_minDamage >= 0 && pHitDesc->m_damage < pDcEntry->m_damageRange->m_minDamage)
			return false;

		if (pDcEntry->m_damageRange->m_maxDamage >= 0 && pHitDesc->m_damage > pDcEntry->m_damageRange->m_maxDamage)
			return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::StoppingPowerRangeMatches(const HitDescription* pHitDesc,
												const DC::HitReactionEntry* pDcEntry) const
{
	if (!pDcEntry || !pHitDesc)
		return false;

	if (pDcEntry->m_stoppingPowerRange)
	{
		if (pDcEntry->m_stoppingPowerRange->m_minSp >= 0.0f
			&& pHitDesc->m_stoppingPower < pDcEntry->m_stoppingPowerRange->m_minSp)
			return false;

		if (pDcEntry->m_stoppingPowerRange->m_maxSp >= 0.0f
			&& pHitDesc->m_stoppingPower > pDcEntry->m_stoppingPowerRange->m_maxSp)
			return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AiHitController::AnimTypeFilter AiHitController::GetAnimTypeFilter(const HitDescription* pHitDesc) const
{
	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_hitReactionMode
							 == AiGameOptions::kHitReactionModeAdditiveOnly))
	{
		return AnimTypeFilter::kAdditiveOnly;
	}

	if (pHitDesc && ((pHitDesc->m_npcState & DC::kHitReactionStateMaskAdditiveOnly) != 0))
	{
		return AnimTypeFilter::kAdditiveOnly;
	}

	if (pHitDesc
		&& (pHitDesc->m_sourceType == DC::kHitReactionSourceTypeMelee
			|| pHitDesc->m_sourceType == DC::kHitReactionSourceTypeMeleeSuperheavy))
	{
		if (pHitDesc->m_hSourceGameObj.IsKindOf(g_type_Npc))
		{
			return AnimTypeFilter::kFullBodyOnly;
		}
		else if (pHitDesc->m_hSourceGameObj.IsKindOf(g_type_PlayerBase))
		{
			return AnimTypeFilter::kAdditiveOnly;
		}
	}

	return AnimTypeFilter::kNone;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::RememberHit(const HitDescription* pHitDesc)
{
	if (!pHitDesc)
		return;

	switch (pHitDesc->m_sourceType)
	{
	case DC::kHitReactionSourceTypeBulletPistol:
	case DC::kHitReactionSourceTypeBulletMg:
	case DC::kHitReactionSourceTypeBulletSg:
	case DC::kHitReactionSourceTypeBulletStun:
	case DC::kHitReactionSourceTypeBulletSniper:
		break;

	default:
		return;
	}

	for (I32F i = kShotHistorySize - 1; i >= 1; --i)
	{
		m_shotHistory[i] = m_shotHistory[i - 1];
	}

	const NavCharacter* pNavChar = GetCharacter();
	m_shotHistory[0] = pNavChar->GetCurTime();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::TestShotCountWindow(const DC::HitReactionShotWindow* pShotWindow) const
{
	const NavCharacter* pNavChar = GetCharacter();
	if (!pShotWindow || !pNavChar)
	{
		// if no shot window specified, always pass this test for filtering hit reactions
		return true;
	}

	const Clock* pClock		   = pNavChar->GetClock();
	const TimeDelta windowTime = Seconds(pShotWindow->m_windowTimeSec);

	U32F shotCount = 0;
	for (U32F i = 0; i < kShotHistorySize; ++i)
	{
		if (pClock->TimePassed(windowTime, m_shotHistory[i]))
			break;

		++shotCount;
	}

	if (shotCount < pShotWindow->m_numShots)
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::ResetShotWindow()
{
	for (U32F i = 0; i < kShotHistorySize; ++i)
	{
		m_shotHistory[i] = TimeFrameNegInfinity();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::TestDistanceValid(const HitDescription* pHitDesc, const DC::AiRangeval* pRangeVal) const
{
	if (!pHitDesc || !pRangeVal)
		return true;

	if (pRangeVal->m_val0 < 0.0f || pRangeVal->m_val1 < 0.0f)
		return true;

	if (pHitDesc->m_distToTarget < 0.0f)
		return true;

	if (pHitDesc->m_distToTarget < pRangeVal->m_val0)
		return false;

	if (pHitDesc->m_distToTarget > pRangeVal->m_val1)
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::TestPlayerDistanceValid(const HitDescription* pHitDesc, const DC::AiRangeval* pRangeVal) const
{
	if (!pHitDesc || !pRangeVal)
		return true;

	if (pRangeVal->m_val0 < 0.0f || pRangeVal->m_val1 < 0.0f)
		return true;

	if (pHitDesc->m_distToPlayer < 0.0f)
		return true;

	if (pHitDesc->m_distToPlayer < pRangeVal->m_val0)
		return false;

	if (pHitDesc->m_distToPlayer > pRangeVal->m_val1)
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::SpeedTestPasses(float curSpeed, const DC::AiRangeval* pRangeVal) const
{
	if (!pRangeVal)
		return true;

	if ((pRangeVal->m_val0 >= 0.0f) && (curSpeed < pRangeVal->m_val0))
	{
		return false;
	}

	if ((pRangeVal->m_val1 >= 0.0f) && (curSpeed > pRangeVal->m_val1))
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::IsCoverRecoveryValid(const PotentialHitAnim& potentialHitAnim,
										   const CoverActionPack* pCoverAp) const
{
	const DC::HitReactionRecoveryList* pRecoveryList = potentialHitAnim.m_pDcEntry
														   ? potentialHitAnim.m_pDcEntry->m_recoveryList
														   : nullptr;
	if (!pRecoveryList || !pCoverAp)
		return false;

	const NavCharacter* pNavChar = GetCharacter();
	const SkeletonId skelId		 = pNavChar->GetSkeletonId();
	const Locator frameWs		 = potentialHitAnim.m_apRef.GetLocatorWs();
	const float exitPhase = Max(potentialHitAnim.m_pDcEntry->m_exitPhase, potentialHitAnim.m_pDcEntry->m_fadeOutPhase);

	if (!pCoverAp->IsAvailableFor(pNavChar))
		return false;

	Locator animEndLocWs = Locator(kIdentity);
	if (!FindAlignFromApReference(skelId,
								  potentialHitAnim.m_pAnim,
								  exitPhase,
								  frameWs,
								  SID("apReference"),
								  &animEndLocWs,
								  potentialHitAnim.m_mirror))
	{
		return false;
	}

	bool valid = false;

	for (U32F iEntry = 0; iEntry < pRecoveryList->m_count; ++iEntry)
	{
		const DC::HitReactionRecoveryEntry& entry = pRecoveryList->m_entries[iEntry];

		if (entry.m_type != DC::kHitReactionRecoveryTypeCover)
			continue;

		SelectedRecovery thisRecovery;
		if (RateRecovery_Cover(entry, animEndLocWs, pCoverAp, &thisRecovery, false) >= 0.0f)
		{
			valid = true;
			break;
		}
	}

	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_hitReaction.m_debugDrawPotentialHitRecoveries))
	{
		for (U32F iEntry = 0; iEntry < pRecoveryList->m_count; ++iEntry)
		{
			const DC::HitReactionRecoveryEntry& entry = pRecoveryList->m_entries[iEntry];

			if (entry.m_type != DC::kHitReactionRecoveryTypeCover)
				continue;

			SelectedRecovery thisRecovery;
			RateRecovery_Cover(entry, animEndLocWs, pCoverAp, &thisRecovery, true);
		}
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::PlayManualAdditiveHR(const StringId64 animId, const float strength, const bool mirror)
{
	NavCharacter* pNavChar	  = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	const bool success = m_additiveAnimAction.FadeToAnim(pAnimControl, animId, 0.0f, 0.1f);
	AnimSimpleLayer* hitReactionLayer = pAnimControl->GetSimpleLayerById(SID("hit-reaction-additive"));
	hitReactionLayer->Fade(strength, 0.0f);

	return success;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::TryQueueHitReactionRecovery()
{
	if (m_hitRecovery.m_pDcDef || !m_pLastSelectedHitEntry || !m_pLastSelectedHitEntry->m_recoveryList)
		return;

	Npc* pNpc = Npc::FromProcess(GetCharacter());
	if (!pNpc)
		return;

	AiPost post;
	CoverActionPack* pCoverApToTryFor = nullptr;
	if (pNpc->IsBuddyNpc())
	{
		pCoverApToTryFor = GetBuddyCoverDiveAp(pNpc, post);
	}
	else
	{
		if (!pNpc->GetIdealPost(DC::kAiPostSelectorPanic, post))
			return;

		if (post.IsValid())
		{
			pCoverApToTryFor = (CoverActionPack*)post.GetActionPack();
		}

		if (pCoverApToTryFor && !pCoverApToTryFor->Reserve(pNpc, kHitReactionCoverReservationTime))
		{
			pCoverApToTryFor = nullptr;
		}
	}

	m_hitRecovery		  = PickHitReactionRecovery(m_pLastSelectedHitEntry, pCoverApToTryFor);
	m_hitRecoverySelected = true;

	if (CoverActionPack* pRecoveryAp = m_hitRecovery.m_hCoverAp.ToActionPack<CoverActionPack>())
	{
#ifdef KOMAR
		if (pNpc->IsBuddyNpc())
		{
			g_prim.Draw(DebugSphere(pRecoveryAp->GetDefensivePosWs(), 1.1f, kColorRed), Seconds(4.0f));
			ScreenSpaceTextPrinter printer(pRecoveryAp->GetDefensivePosWs() + Vector(0.0f, 1.5f, 0.0f));
			printer.SetDuration(Seconds(4.0f));
			printer.PrintText(kColorRed, "COVERDIVE");
		}
#endif

		ANIM_ASSERT(pRecoveryAp == pCoverApToTryFor);
		ANIM_ASSERT(pRecoveryAp->IsAvailableFor(pNpc));

		AI_ASSERT(post.IsValid());
		pNpc->AssignToPost(post);
	}
	else if (pCoverApToTryFor && (pCoverApToTryFor->IsReservedBy(pNpc)))
	{
		pCoverApToTryFor->Release(pNpc);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
AiHitController::SelectedRecovery AiHitController::PickHitReactionRecovery(const DC::HitReactionEntry* pEntry,
																		   const CoverActionPack* pCoverApToTryFor) const
{
	const DC::HitReactionRecoveryList* pList = pEntry ? pEntry->m_recoveryList : nullptr;
	if (!pList)
		return SelectedRecovery();

	const NavCharacter* pNavChar = GetCharacter();
	const Locator& parentSpace	 = pNavChar->GetParentSpace();
	const Locator animEndLocWs	 = parentSpace.TransformLocator(m_fullBodyExitAlignPs);

	SelectedRecovery ret;

	float bestRating = kLargeFloat;

	const bool coverValid = pCoverApToTryFor && pCoverApToTryFor->IsAvailableFor(pNavChar);

	for (U32F iEntry = 0; iEntry < pList->m_count; ++iEntry)
	{
		const DC::HitReactionRecoveryEntry& recovery = pList->m_entries[iEntry];

		float rating = -1.0f;

		SelectedRecovery thisRecovery;

		switch (recovery.m_type)
		{
		case DC::kHitReactionRecoveryTypeIdle:
			rating = RateRecovery_Idle(recovery, &thisRecovery);
			break;
		case DC::kHitReactionRecoveryTypeMoving:
			rating = RateRecovery_Moving(recovery, &thisRecovery);
			break;
		case DC::kHitReactionRecoveryTypeCover:
			rating = coverValid ? RateRecovery_Cover(recovery, animEndLocWs, pCoverApToTryFor, &thisRecovery, false)
								: -1.0f;
			break;
		}

		if (rating < 0.0f)
			continue;

		if (rating < bestRating)
		{
			bestRating = rating;
			ret		   = thisRecovery;
		}
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiHitController::RateRecovery_Idle(const DC::HitReactionRecoveryEntry& recovery,
										 SelectedRecovery* pRecoveryOut) const
{
	const NavCharacter* pNavChar	= GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();

	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(recovery.m_anim).ToArtItem();
	if (!pAnim)
		return -1.0f;

	pRecoveryOut->m_pDcDef	   = &recovery;
	pRecoveryOut->m_startPhase = 0.0f;
	pRecoveryOut->m_initialAp = pRecoveryOut->m_finalAp = pNavChar->GetBoundFrame();
	pRecoveryOut->m_selfBlend.m_phase = -1.0f;

	if (recovery.m_selfBlend)
	{
		pRecoveryOut->m_selfBlend = *recovery.m_selfBlend;
	}
	else
	{
		pRecoveryOut->m_selfBlend.m_phase = 0.0f;
		pRecoveryOut->m_selfBlend.m_time  = GetDuration(pAnim);
		pRecoveryOut->m_selfBlend.m_curve = DC::kAnimCurveTypeUniformS;
	}

	return 100.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiHitController::RateRecovery_Moving(const DC::HitReactionRecoveryEntry& recovery,
										   SelectedRecovery* pRecoveryOut) const
{
	const NavCharacter* pNavChar = GetCharacter();
	const NavStateMachine* pNsm	 = pNavChar->GetNavStateMachine();
	if (!pNsm->IsCommandInProgress() || pNsm->IsCommandStopAndStand())
		return -1.0f;

	pRecoveryOut->m_pDcDef	   = &recovery;
	pRecoveryOut->m_startPhase = 0.0f;
	pRecoveryOut->m_initialAp = pRecoveryOut->m_finalAp = pNavChar->GetBoundFrame();
	pRecoveryOut->m_selfBlend.m_phase = -1.0f;

	return 50.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiHitController::RateRecovery_Cover(const DC::HitReactionRecoveryEntry& recovery,
										  const Locator& setupEndLocWs,
										  const CoverActionPack* pCoverAp,
										  SelectedRecovery* pRecoveryOut,
										  bool debugDraw) const
{
	if (!pCoverAp)
		return -1.0f;

	const NavCharacter* pNavChar	= GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();
	const SkeletonId skelId			= pNavChar->GetSkeletonId();

	pRecoveryOut->m_pDcDef = &recovery;

	pRecoveryOut->m_initialAp = pRecoveryOut->m_finalAp = pCoverAp->GetBoundFrame();

	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(recovery.m_anim).ToArtItem();
	if (!pAnim)
		return -1.0f;

	const IAiCoverController* pCoverController = pNavChar->GetAnimationControllers()->GetCoverController();

	if (!pCoverController)
		return -1.0f;

	const BoundFrame coverApRef = pCoverController->GetApReferenceForCover(pCoverAp, CoverActionPack::kPreferNone);
	const Locator coverApRefWs	= coverApRef.GetLocatorWs();

	const float minPhase = recovery.m_phaseRange ? Limit01(recovery.m_phaseRange->m_val0) : 0.0f;
	const float maxPhase = recovery.m_phaseRange ? Limit01(recovery.m_phaseRange->m_val1) : 1.0f;

	Locator desiredEndAlignWs;
	if (!FindAlignFromApReference(skelId, pAnim, 1.0f, coverApRefWs, SID("apReference"), &desiredEndAlignWs))
	{
		return -1.0f;
	}

	Locator naturalApRefWs;
	if (!FindApReferenceFromAlign(skelId, pAnim, setupEndLocWs, &naturalApRefWs, 0.0f))
	{
		return -1.0f;
	}

	Locator naturalEndAlignWs;
	if (!FindAlignFromApReference(skelId, pAnim, 1.0f, naturalApRefWs, SID("apReference"), &naturalEndAlignWs))
	{
		return -1.0f;
	}

	Locator minAlignLs;
	if (!EvaluateChannelInAnim(skelId, pAnim, SID("align"), minPhase, &minAlignLs))
	{
		return -1.0f;
	}

	float phaseDistErr = -1.0f;

	const float initialDist = DistXz(minAlignLs.Pos(), kOrigin);
	const float matchDist	= DistXz(coverApRef.GetTranslationWs(), setupEndLocWs.Pos()) - initialDist;

	PhaseMatchParams phaseParams;
	phaseParams.m_minPhase	   = minPhase;
	phaseParams.m_maxPhase	   = maxPhase;
	phaseParams.m_pBestDistOut = &phaseDistErr;

	const float skipPhase = ComputePhaseToMatchDistance(skelId, pAnim, matchDist, phaseParams);

	Locator natSkipEndAlignWs = naturalEndAlignWs;

	pRecoveryOut->m_hCoverAp  = pCoverAp;
	pRecoveryOut->m_initialAp = pRecoveryOut->m_finalAp = coverApRef;
	pRecoveryOut->m_finalAp	   = coverApRef;
	pRecoveryOut->m_startPhase = 0.0f;
	pRecoveryOut->m_skipPhase  = -1.0f;

	if ((skipPhase > minPhase) && ((skipPhase - NDI_FLT_EPSILON) < maxPhase))
	{
		pRecoveryOut->m_skipPhase = skipPhase;

		Locator skipStartAlignWs;
		if (!FindAlignFromApReference(skelId, pAnim, minPhase, naturalApRefWs, SID("apReference"), &skipStartAlignWs))
		{
			return -1.0f;
		}

		Locator natSkipApRefWs = naturalApRefWs;
		if (!FindApReferenceFromAlign(skelId, pAnim, skipStartAlignWs, &natSkipApRefWs, skipPhase))
		{
			return -1.0f;
		}

		if (!FindAlignFromApReference(skelId, pAnim, 1.0f, natSkipApRefWs, SID("apReference"), &natSkipEndAlignWs))
		{
			return -1.0f;
		}
	}

	Locator initialApRefWs;
	{
		const float maxInitialApRotDeg = 45.0f;
		const Vector natTravelDir	   = SafeNormalize(natSkipEndAlignWs.Pos() - setupEndLocWs.Pos(), kUnitZAxis);
		const Vector desTravelDir	   = SafeNormalize(desiredEndAlignWs.Pos() - setupEndLocWs.Pos(), kUnitZAxis);

		const Quat initalApRotAdj	   = QuatFromVectors(natTravelDir, desTravelDir);
		const Quat limitedApRotAdj	   = LimitQuatAngle(initalApRotAdj, DEGREES_TO_RADIANS(maxInitialApRotDeg));
		const Locator adjSetupEndLocWs = Locator(setupEndLocWs.Pos(), setupEndLocWs.Rot() * limitedApRotAdj);

		if (!FindApReferenceFromAlign(skelId, pAnim, adjSetupEndLocWs, &initialApRefWs, 0.0f))
		{
			return -1.0f;
		}
	}

	pRecoveryOut->m_initialAp.SetLocatorWs(initialApRefWs);

	const Locator animApToEndLs = naturalApRefWs.UntransformLocator(natSkipEndAlignWs);
	const Locator computedEndAlignWs = initialApRefWs.TransformLocator(animApToEndLs);
	const float alignDistErr		 = Dist(computedEndAlignWs.Pos(), desiredEndAlignWs.Pos());

	const Vector computedEndFacingWs = GetLocalZ(computedEndAlignWs.Rot());
	const Vector desiredEndFacingWs	 = GetLocalZ(desiredEndAlignWs.Rot());
	const float rotErrDeg = RADIANS_TO_DEGREES(SafeAcos(Dot(computedEndFacingWs, desiredEndFacingWs)));

	if (recovery.m_selfBlend)
	{
		pRecoveryOut->m_selfBlend = *recovery.m_selfBlend;
	}
	else
	{
		pRecoveryOut->m_selfBlend.m_phase = 0.0f;
		pRecoveryOut->m_selfBlend.m_time  = GetDuration(pAnim);
		pRecoveryOut->m_selfBlend.m_curve = DC::kAnimCurveTypeUniformS;
	}

	bool valid = true;

	if (phaseDistErr > recovery.m_maxDistErr)
	{
		valid = false;
	}

	if (rotErrDeg > recovery.m_maxRotErrDeg)
	{
		valid = false;
	}

	if (alignDistErr > recovery.m_maxDistErr)
	{
		valid = false;
	}

	bool hasClearMotion = false;
	if (valid)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
		const NavControl* pNavCon = pNavChar->GetNavControl();

		const NavMesh* pNavMesh = pNavCon->GetNavMesh();

		if (!pNavMesh)
		{
			return -1.0f;
		}

		// setupEndLocWs -> minPhaseAlignWs -> maxPhaseAlignWs (or skip align) -> desiredEndAlignWs
		Point testPointsWs[4];
		testPointsWs[0] = setupEndLocWs.Pos();

		{
			Locator minAlignApLs;
			if (!FindAlignFromApReference(skelId, pAnim, minPhase, Locator(kIdentity), SID("apReference"), &minAlignApLs))
			{
				return -1.0f;
			}

			testPointsWs[1] = pRecoveryOut->m_initialAp.GetLocatorWs().TransformLocator(minAlignApLs).Pos();
		}

		{
			float testPhase = maxPhase;
			if ((skipPhase > minPhase) && ((skipPhase - NDI_FLT_EPSILON) < maxPhase))
			{
				testPhase = skipPhase;
			}

			Locator maxAlignApLs;
			if (!FindAlignFromApReference(skelId, pAnim, testPhase, Locator(kIdentity), SID("apReference"), &maxAlignApLs))
			{
				return -1.0f;
			}

			testPointsWs[2] = pRecoveryOut->m_finalAp.GetLocatorWs().TransformLocator(maxAlignApLs).Pos();
		}

		testPointsWs[3] = desiredEndAlignWs.Pos();

		hasClearMotion = true;

		NavMesh::ProbeParams params;
		params.m_dynamicProbe = false;
		params.m_probeRadius  = 0.0f;
		params.m_obeyedStaticBlockers = pNavCon->GetObeyedStaticBlockers();

		for (U32F i = 0; i < ARRAY_COUNT(testPointsWs) - 1; ++i)
		{
			const Point p0Ws = testPointsWs[i];
			const Point p1Ws = testPointsWs[i + 1];
			params.m_start	 = pNavMesh->WorldToLocal(p0Ws);
			params.m_move	 = pNavMesh->WorldToLocal(p1Ws) - params.m_start;

			if (pNavMesh->ProbeLs(&params) != NavMesh::ProbeResult::kReachedGoal)
			{
				if (FALSE_IN_FINAL_BUILD(debugDraw && g_aiGameOptions.m_hitReaction.m_debugDrawPotentialHitRecoveries))
				{
					g_prim.Draw(DebugArrow(p0Ws, p1Ws, kColorRed), Seconds(2.0f));
					g_prim.Draw(DebugCross(pNavMesh->LocalToWorld(params.m_impactPoint), 0.2f, kColorRed),
								Seconds(2.0f));
				}

				hasClearMotion = false;
				break;
			}
			// 			else if (FALSE_IN_FINAL_BUILD(debugDraw && g_aiGameOptions.m_hitReaction.m_debugDrawPotentialHitRecoveries))
			// 			{
			// 				g_prim.Draw(DebugArrow(p0Ws, p1Ws, kColorGreenTrans), Seconds(2.0f));
			// 			}
		}
	}

	const float rating = (valid && hasClearMotion) ? (alignDistErr + phaseDistErr + (0.25f * rotErrDeg)) : -1.0f;

	if (FALSE_IN_FINAL_BUILD(debugDraw && g_aiGameOptions.m_hitReaction.m_debugDrawPotentialHitRecoveries))
	{
		const float textScale  = 0.5f;
		const DebugPrimTime tt = Seconds(5.0f);

		TextPrinterWorldSpace tp = TextPrinterWorldSpace(tt);
		tp.SetTextScale(textScale);

		g_prim.Draw(DebugCoordAxes(computedEndAlignWs), tt);
		g_prim.Draw(DebugCoordAxes(desiredEndAlignWs), tt);
		g_prim.Draw(DebugSphere(desiredEndAlignWs.Pos(), 0.1f), tt);
		g_prim.Draw(DebugLine(setupEndLocWs.Pos(),
							  computedEndAlignWs.Pos(),
							  (phaseDistErr > recovery.m_maxDistErr) ? kColorRed : kColorGreen),
					tt);
		g_prim.Draw(DebugLine(computedEndAlignWs.Pos(),
							  desiredEndAlignWs.Pos(),
							  (alignDistErr > recovery.m_maxDistErr) ? kColorRed : kColorGreen),
					tt);

		tp.Start(computedEndAlignWs.Pos());
		tp.PrintF(valid ? kColorWhite : kColorRed, "[%s]\n", pAnim->GetName());
		tp.PrintF((phaseDistErr > recovery.m_maxDistErr) ? kColorRed : kColorWhite,
				  "skipPhase: %f\n",
				  pRecoveryOut->m_skipPhase);
		tp.PrintF((phaseDistErr > recovery.m_maxDistErr) ? kColorRed : kColorWhite,
				  "phaseDistErr: %0.4fm\n",
				  phaseDistErr);
		tp.PrintF((alignDistErr > recovery.m_maxDistErr) ? kColorRed : kColorWhite,
				  "alignDistErr: %0.4fm\n",
				  alignDistErr);
		tp.PrintF((rotErrDeg > recovery.m_maxRotErrDeg) ? kColorRed : kColorWhite, "rotErrDeg: %0.1fdeg\n", rotErrDeg);
		// 		tp.PrintF(valid ? kColorWhite : kColorRed, "startPhase: %f\n", pRecoveryOut->m_startPhase);

		if (valid && !hasClearMotion)
		{
			tp.PrintF(kColorRed, "No Clear Motion\n");
		}

		if (valid && hasClearMotion)
		{
			tp.PrintF(kColorWhite, "rating: %f\n", rating);
		}
		else
		{
			tp.PrintF(kColorRed, "rating: INVALID\n");
		}

		tp.End();

		g_prim.Draw(DebugArrow(computedEndAlignWs.Pos(),
							   computedEndFacingWs,
							   (rotErrDeg > recovery.m_maxRotErrDeg) ? kColorRed : kColorWhite),
					tt);
		g_prim.Draw(DebugArrow(computedEndAlignWs.Pos(),
							   desiredEndFacingWs,
							   (rotErrDeg > recovery.m_maxRotErrDeg) ? kColorRedTrans : kColorWhiteTrans),
					tt);
	}

	return rating;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::ValidateHitReactions() const
{
	STRIP_IN_FINAL_BUILD;

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	const NavCharacter* pNavChar = GetCharacter();
	const NdAnimControllerConfig* pConfig = pNavChar->GetAnimControllerConfig();

	const CompositeBody* pCompBody = pNavChar->GetCompositeBody();
	const U32F numBodies	 = pCompBody ? pCompBody->GetNumBodies() : 0;
	StringId64* pJointIdList = (numBodies > 0) ? (NDI_NEW StringId64[numBodies]) : nullptr;

	for (U32F iBody = 0; iBody < numBodies; ++iBody)
	{
		const RigidBody* pBody = pCompBody->GetBody(iBody);
		pJointIdList[iBody]	   = pBody ? CharacterCollision::GetBodyAttachPoint(pBody) : INVALID_STRING_ID_64;
	}

	ValidateHitReactionsSet("HIT", pConfig->m_hitReaction.m_hitReactionSetId, pJointIdList, numBodies);

	ValidateHitReactionsSet("DEATH", pConfig->m_hitReaction.m_deathReactionSetId, pJointIdList, numBodies);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::ValidateHitReactionsSet(const char* typeStr,
											  const StringId64 setId,
											  const StringId64* pJointIdList,
											  const U32F numJoints) const
{
	STRIP_IN_FINAL_BUILD;

	const NavCharacter* pNavChar = GetCharacter();

	MsgOut("================================================================================================\n");

	const DC::Map* pHitMap = ScriptManager::Lookup<const DC::Map>(setId, nullptr);
	if (!pHitMap)
	{
		SetForegroundColor(kMsgOut, kTextColorRed);
		MsgOut("[%s] !! Invalid reaction set specified !! '%s'\n",
			   pNavChar->GetName(),
			   DevKitOnly_StringIdToString(setId));
		SetForegroundColor(kMsgOut, kTextColorNormal);
		return;
	}

	MsgOut("Beginning hit reaction validation for '%s' setId: '%s' \n",
		   pNavChar->GetName(),
		   DevKitOnly_StringIdToString(setId));

	StringId64 armorList[512];
	U32F numArmorPieces = 0;

	for (U32F iJoint = 0; iJoint < numJoints; ++iJoint)
	{
		MsgOut(" Found attach joint '%s'", DevKitOnly_StringIdToString(pJointIdList[iJoint]));

		const bool hasArmorProtection = SendEvent(SID("attach-has-armor-protection?"),
												  (Process*)pNavChar,
												  BoxedValue(pJointIdList[iJoint]))
											.GetAsBool(false);
		if (!hasArmorProtection)
		{
			MsgOut("\n");
			continue;
		}

		MsgOut(" (WITH armor protection)\n");

		armorList[numArmorPieces++] = pJointIdList[iJoint];
	}

	const Npc* pNpc		 = Npc::FromProcess(pNavChar);
	const bool hasHelmet = pNpc && pNpc->HasHelmet();

	if (hasHelmet)
	{
		MsgOut(" Found helmet (joint: targHelmet)\n");
		armorList[numArmorPieces++] = SID("targHelmet");
	}

	const U32F numBuckets = pHitMap->m_count;
	bool foundDefault	  = false;

	for (U32F iBucket = 0; iBucket < numBuckets; ++iBucket)
	{
		const StringId64 bucketId = pHitMap->m_keys[iBucket];
		foundDefault = (bucketId == SID("default"));

		for (U32F iOtherBucket = 0; iOtherBucket < numBuckets; ++iOtherBucket)
		{
			if (iOtherBucket == iBucket)
				continue;
			const StringId64 otherBucketId = pHitMap->m_keys[iOtherBucket];
			if (otherBucketId == bucketId)
			{
				SetForegroundColor(kMsgOut, kTextColorRed);
				MsgOut(" ERROR: Duplicate hit reaction buckets found for bucket type '%s'\n",
					   DevKitOnly_StringIdToString(bucketId));
				SetForegroundColor(kMsgOut, kTextColorNormal);
			}
		}

		const DC::HitReactionEntryList* pHitList = (const DC::HitReactionEntryList*)pHitMap->m_data[iBucket].m_ptr;
		if (!pHitList)
		{
			SetForegroundColor(kMsgOut, kTextColorRed);
			MsgOut(" ERROR: Bucket '%s' specified with no data!\n", DevKitOnly_StringIdToString(bucketId));
			SetForegroundColor(kMsgOut, kTextColorNormal);
			continue;
		}

		ValidateHitReactionsBucket(typeStr, setId, bucketId, pHitList, pJointIdList, numJoints, armorList, numArmorPieces);
	}

	MsgOut("================================================================================================\n");
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TestDirMask(const DC::HitReactionDirectionMask requestedDir, const DC::HitReactionDirectionMask testMask)
{
	if (testMask == 0)
		return true;

	// trivial
	if (requestedDir == testMask)
		return true;

	float requestedAngleMin = 0.0f;
	float requestedAngleMax = 0.0f;

	switch (requestedDir)
	{
	case DC::kHitReactionDirectionMaskFront90:
		return 0 != (testMask & (DC::kHitReactionDirectionMaskFront90 | DC::kHitReactionDirectionMaskFront180));

	case DC::kHitReactionDirectionMaskBack90:
		return 0 != (testMask & (DC::kHitReactionDirectionMaskBack90 | DC::kHitReactionDirectionMaskBack180));

	case DC::kHitReactionDirectionMaskLeft90:
		return 0 != (testMask & (DC::kHitReactionDirectionMaskLeft90 | DC::kHitReactionDirectionMaskLeft180));

	case DC::kHitReactionDirectionMaskRight90:
		return 0 != (testMask & (DC::kHitReactionDirectionMaskRight90 | DC::kHitReactionDirectionMaskRight180));
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::ValidateHitReactionsStateRange(const char* typeStr,
													 DC::HitReactionStateMask startMask,
													 DC::HitReactionStateMask endMask,
													 DC::HitReactionStateMask maskIgnore,
													 const StringId64 setId,
													 const StringId64 bucketId,
													 const DC::HitReactionEntryList* pHitList,
													 const StringId64* pJointIdList,
													 const U32F numJoints) const
{
	const NavCharacter* pNavChar = GetCharacter();
	const U32F numDemeanors		 = pNavChar->GetNumDemeanors();

	const DC::HitReactionDirectionMask dirMaskList[4] = { DC::kHitReactionDirectionMaskFront90,
														  DC::kHitReactionDirectionMaskBack90,
														  DC::kHitReactionDirectionMaskLeft90,
														  DC::kHitReactionDirectionMaskRight90 };

	// for every demeanor, test every state
	for (U64 baseStateMask = startMask; (baseStateMask <= endMask) && (baseStateMask < DC::kHitReactionStateMaskMax);
		 baseStateMask	   = baseStateMask << 1)
	{
		const U64 stateMask = baseStateMask & ~maskIgnore;

		if (stateMask == 0)
			continue;

		for (U32F iDemeanor = 0; iDemeanor < numDemeanors; ++iDemeanor)
		{
			const DC::NpcDemeanor curDemeanor = DcDemeanorFromDemeanor(Demeanor(iDemeanor));
			if (!pNavChar->HasDemeanor(iDemeanor))
				continue;

			// for every state, test every attack direction (using smallest direction increment, 90 deg)
			for (U32F dirMaskIdx = 0; dirMaskIdx < 4; ++dirMaskIdx)
			{
				const DC::HitReactionDirectionMask dirMask = dirMaskList[dirMaskIdx];

				// for every direction, test every attack type
				for (U32F attackType = DC::kHitReactionSourceTypeBulletPistol;
					 attackType <= DC::kHitReactionSourceTypeBulletSg;
					 attackType = attackType << 1)
				{
					// for every attack type, test every hit location
					for (U32F hitJointIndex = 0; hitJointIndex < numJoints; ++hitJointIndex)
					{
						const StringId64 hitJointId = pJointIdList[hitJointIndex];
						if (INVALID_STRING_ID_64 == hitJointId)
							continue;

						const bool hitReaction	= 0 == strcmp(typeStr, "HIT"); // BOOO!!
						const bool testingArmor = (0 != (stateMask & kArmorMask));
						if (hitReaction && hitJointId == SID("targHead") && !testingArmor)
							continue;

						bool reactionFound = false;

						for (U32F currentIndex = 0; currentIndex < pHitList->m_count; ++currentIndex)
						{
							const DC::HitReactionEntry& currentHitAnim = pHitList->m_array[currentIndex];

							if (currentHitAnim.m_animId == INVALID_STRING_ID_64)
								continue;

							const DC::NpcDemeanorMask requiredDem = currentHitAnim.m_requiredDemeanor;
							if ((requiredDem > 0) && !(requiredDem & (1UL << curDemeanor)))
								continue;

							// can't rely on shot window animations always being valid, so don't count them towards filling in missing hit reactions
							if (currentHitAnim.m_shotWindow)
								continue;

							if (currentHitAnim.m_damageRange)
								continue;

							if (currentHitAnim.m_stoppingPowerRange)
								continue;

							if ((currentHitAnim.m_flags & DC::kHitReactionFlagsFireBackOnly) != 0)
								continue;

							// same for distance restricted
							if (currentHitAnim.m_validDistRange)
								continue;

							if (0 == (currentHitAnim.m_sourceType & attackType))
								continue;

							if (currentHitAnim.m_stateMask && (0 == (currentHitAnim.m_stateMask & stateMask)))
								continue;

							if ((currentHitAnim.m_dirMask != 0) && !TestDirMask(dirMask, currentHitAnim.m_dirMask))
								continue;

							if (!HitJointIdMatches(hitJointId, &currentHitAnim))
								continue;

							reactionFound = true;
							break;
						}

						if (!reactionFound)
						{
							SetForegroundColor(kMsgOut, kTextColorRed);

							MsgOut("  [bucket %s] ERROR: %s Reaction Missing! State: '%s' Demeanor: '%s' Direction: '%s' AttackType: '%s' HitJoint: '%s'\n",
								   DevKitOnly_StringIdToString(bucketId),
								   typeStr,
								   GetNpcStateName(stateMask),
								   pNavChar->GetDemeanorName(Demeanor(iDemeanor)),
								   GetDirectionMaskName(dirMask),
								   GetAttackTypeName(attackType),
								   DevKitOnly_StringIdToString(hitJointId));

							SetForegroundColor(kMsgOut, kTextColorNormal);
						}
					}
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiHitController::ValidateHitReactionsBucket(const char* typeStr,
												 const StringId64 setId,
												 const StringId64 bucketId,
												 const DC::HitReactionEntryList* pHitList,
												 const StringId64* pJointIdList,
												 const U32F numJoints,
												 const StringId64* armorList,
												 const U32F numArmorPieces) const
{
	STRIP_IN_FINAL_BUILD;
	ASSERT(pHitList);
	if (!pHitList)
		return;

	const NavCharacter* pNavChar	= GetCharacter();
	const AnimControl* pAnimControl = pNavChar->GetAnimControl();

	MsgOut(" Beginning hit reaction validation for bucket '%s' in set '%s'\n",
		   DevKitOnly_StringIdToString(bucketId),
		   DevKitOnly_StringIdToString(setId));

	for (U32F currentIndex = 0; currentIndex < pHitList->m_count; ++currentIndex)
	{
		const DC::HitReactionEntry& currentHitAnim = pHitList->m_array[currentIndex];

		ASSERT(currentHitAnim.m_animId != INVALID_STRING_ID_64);
		const ArtItemAnim* pArtItem = pAnimControl->LookupAnim(currentHitAnim.m_animId).ToArtItem();
		if (!pArtItem)
		{
			SetForegroundColor(kMsgOut, kTextColorRed);
			MsgOut("  [bucket %s] ERROR: HitReaction Animation '%s' is not loaded or do not exist (index: %d)\n",
				   DevKitOnly_StringIdToString(bucketId),
				   pAnimControl->DevKitOnly_LookupAnimName(currentHitAnim.m_animId),
				   int(currentIndex));
			SetForegroundColor(kMsgOut, kTextColorNormal);
		}
	}

	DC::HitReactionStateMask baseIgnoreFlags = DC::kHitReactionStateMaskHeal | DC::kHitReactionStateMaskAdditiveOnly
											   | DC::kHitReactionStateMaskSpawning | DC::kHitReactionStateMaskTurret
											   | DC::kHitReactionStateMaskPerch | DC::kHitReactionStateMaskClimbing
											   | DC::kHitReactionStateMaskMeleeDown;

	if (const Infected* pInfected = Infected::FromProcess(pNavChar))
	{
		baseIgnoreFlags = baseIgnoreFlags | DC::kHitReactionStateMaskShieldNormal
						  | DC::kHitReactionStateMaskShieldCaution | DC::kHitReactionStateMaskShieldHunker
						  | DC::kHitReactionStateMaskShieldTurtle | DC::kHitReactionStateMaskVehicleTypeJeep
						  | DC::kHitReactionStateMaskVehicleTypeTruck | DC::kHitReactionStateMaskVehicleTypeBike
						  | DC::kHitReactionStateMaskVehicleTypeTurretTruck | DC::kHitReactionStateMaskVehicleReadyJump
						  | DC::kHitReactionStateMaskVehicleJumping | DC::kHitReactionStateMaskVehicleLanding
						  | DC::kHitReactionStateMaskVehicleHangingOn | DC::kHitReactionStateMaskVehicleIdle
						  | DC::kHitReactionStateMaskVehicleAimingFront | DC::kHitReactionStateMaskVehicleAimingBack
						  | DC::kHitReactionStateMaskVehicleRoleDriver
						  | DC::kHitReactionStateMaskVehicleRolePassengerShotgun
						  | DC::kHitReactionStateMaskVehicleRolePassengerBack;

		if (!pInfected->HasSkill(DC::kAiArchetypeSkillInfectedAmbush))
		{
			baseIgnoreFlags = baseIgnoreFlags | DC::kHitReactionStateMaskCoverHighLeft
							  | DC::kHitReactionStateMaskCoverHighRight | DC::kHitReactionStateMaskCoverLowLeft
							  | DC::kHitReactionStateMaskCoverLowRight | DC::kHitReactionStateMaskCoverHighLeftAim
							  | DC::kHitReactionStateMaskCoverHighRightAim | DC::kHitReactionStateMaskCoverLowLeftAim
							  | DC::kHitReactionStateMaskCoverLowRightAim | DC::kHitReactionStateMaskCoverLowLeftOverAim
							  | DC::kHitReactionStateMaskCoverLowRightOverAim | DC::kHitReactionStateMaskCoverInjured;
		}

		if (!pInfected->GetParams()->m_canFrenzy)
		{
			baseIgnoreFlags = baseIgnoreFlags | DC::kHitReactionStateMaskInfectedFrenzy;
		}
	}
	else
	{
		baseIgnoreFlags = baseIgnoreFlags | DC::kHitReactionStateMaskInfectedSleeping
						  | DC::kHitReactionStateMaskInfectedMeleeCharge | DC::kHitReactionStateMaskInfectedFrenzy;
	}

	if (bucketId == SID("default"))
	{
		ValidateHitReactionsStateRange(typeStr,
									   0x1,
									   DC::kHitReactionStateMaskMax,
									   baseIgnoreFlags | kArmorMask,
									   setId,
									   bucketId,
									   pHitList,
									   pJointIdList,
									   numJoints);
	}
	else
	{
		ValidateHitReactionsStateRange(typeStr,
									   DC::kHitReactionStateMaskIdle,
									   DC::kHitReactionStateMaskIdle,
									   baseIgnoreFlags | kArmorMask,
									   setId,
									   bucketId,
									   pHitList,
									   pJointIdList,
									   numJoints);
	}

	if (numArmorPieces > 0)
	{
		ValidateHitReactionsStateRange(typeStr,
									   DC::kHitReactionStateMaskArmor,
									   DC::kHitReactionStateMaskArmor,
									   baseIgnoreFlags,
									   setId,
									   bucketId,
									   pHitList,
									   armorList,
									   numArmorPieces);

		ValidateHitReactionsStateRange(typeStr,
									   DC::kHitReactionStateMaskArmorDestroyed,
									   DC::kHitReactionStateMaskArmorDestroyed,
									   baseIgnoreFlags,
									   setId,
									   bucketId,
									   pHitList,
									   armorList,
									   numArmorPieces);

		ValidateHitReactionsStateRange(typeStr,
									   DC::kHitReactionStateMaskArmorMasterDestroyed,
									   DC::kHitReactionStateMaskArmorMasterDestroyed,
									   baseIgnoreFlags,
									   setId,
									   bucketId,
									   pHitList,
									   armorList,
									   numArmorPieces);
	}

	MsgOut(" Finished bucket '%s'\n", DevKitOnly_StringIdToString(bucketId));
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiHitController::DeathAnimWhitelistedForRagdollGroundProbesFix() const
{
	// T2 Ship it

	static const StringId64 s_aWhitelist [] = {
		SID("npc-m-death-molotov-bw"),
		SID("npc-m-death-molotov-fw"),
		SID("npc-m-death-molotov-lt"),
		SID("npc-m-death-molotov-rt"),
		SID("npc-m-death-molotov-fw-a"),
		SID("npc-m-death-molotov-lt-a"),
		SID("npc-m-death-molotov-rt-a")
	};

	if (m_pLastSelectedHitEntry)
	{
		for (U32 ii = 0; ii<sizeof(s_aWhitelist)/sizeof(s_aWhitelist[0]); ii++)
		{
			if (m_pLastSelectedHitEntry->m_animId == s_aWhitelist[ii])
			{
				return true;
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
#if !FINAL_BUILD
void AiHitController::MailMissingHitReactionInfo(const HitDescription* pHitDesc,
												 const char* typeStr,
												 StringId64 setId) const
{
	return; // disable for now

	const NavCharacter* pNavChar = GetCharacter();

	if (!IsEnemy(pNavChar->GetFactionId(), FactionIdPlayer()))
		return;

	switch (pHitDesc->m_sourceType)
	{
	case DC::kHitReactionSourceTypeMelee:
	case DC::kHitReactionSourceTypeShove:
		return;
	}

	if (pNavChar->GetLookId() == SID("roberts"))
		return;

	if (pNavChar->IsKindOf(SID("David")))
		return;

	char fromAddr[256];

	if (strlen(g_realUserName) <= 0)
		sprintf(fromAddr, "unknown-devkit@naughtydog.com");
	else
		sprintf(fromAddr, "%s-devkit@naughtydog.com", g_realUserName);

	beginMail("michal_mach@naughtydog.com; john_bellomy@naughtydog.com",
			  StringBuilder<128>("[%s Reaction Not Found]", typeStr).c_str(),
			  fromAddr);

	const AnimControl* pAnimControl	 = pNavChar->GetAnimControl();
	const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	const AnimStateInstance* pCurInstance = pBaseLayer->CurrentStateInstance();
	const ArtItemAnim* pCurAnim = pCurInstance ? pCurInstance->GetPhaseAnimArtItem().ToArtItem() : nullptr;

	{
		ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);

		const U32 kMailBufferSize = 2 * 1024;
		char* pMailBuffer		  = NDI_NEW char[kMailBufferSize];
		StringBuilderExternal* pSb = StringBuilderExternal::Create(pMailBuffer, kMailBufferSize);

		const StringId64 activeBucketId = GetCurrentActiveBucket();
		const StringId64 bucketId		= (activeBucketId != INVALID_STRING_ID_64) ? activeBucketId : SID("default");
		const AnimTypeFilter animTypeFilter = GetAnimTypeFilter(pHitDesc);

		pSb->append_format("\nNpc '%s' tried to play a %s reaction but couldn't find one.\n\n"
						   "P4 Changelist: %d\n\n"
						   "Parmaters:\n"
						   " Look:\t\t%s\n"
						   " Set ID:\t\t%s\n"
						   " Bucket:\t%s\n"
						   " Direction:\t%s\n"
						   " Hit Joint:\t%s\n"
						   " Attack Type:\t%s\n"
						   " State:\t\t%s\n"
						   " Additive Only:\t%s\n"
						   " Full-Body Only:\t%s\n"
						   " Damage:\t%d\n"
						   " Cur. Anim:\t%s\n"
						   " Demeanor:\t%s\n"
						   " Dist To Targ:\t%0.1fm\n"
						   " Stopping Pwr:\t%f\n"
						   " Health:\t%0.1f%% -> %0.1f%%\n"
						   "%s"
						   "%s"
						   "%s"
						   "\n",
						   pNavChar->GetName(),
						   typeStr,
						   g_ndConfig.m_p4ChangeNumber,
						   DevKitOnly_StringIdToString(pNavChar->GetLookId()),
						   DevKitOnly_StringIdToString(setId),
						   DevKitOnly_StringIdToString(bucketId),
						   GetDirectionMaskName(GetHitDirection(*pHitDesc)),
						   DevKitOnly_StringIdToString(GetHitJointId(*pHitDesc)),
						   GetAttackTypeName(pHitDesc->m_sourceType),
						   GetNpcStateName(pHitDesc->m_npcState),
						   animTypeFilter == AnimTypeFilter::kAdditiveOnly ? "TRUE" : "false",
						   animTypeFilter == AnimTypeFilter::kFullBodyOnly ? "TRUE" : "false",
						   pHitDesc->m_damage,
						   pCurAnim ? pCurAnim->GetName() : "<none>",
						   pNavChar->GetDemeanorName(pNavChar->GetCurrentDemeanor()),
						   pHitDesc->m_distToTarget,
						   pHitDesc->m_stoppingPower,
						   pHitDesc->m_prevNormalizedHealth * 100.0f,
						   pHitDesc->m_normalizedHealth * 100.0f,
						   pHitDesc->m_headShot ? " [Headshot]\n" : "",
						   pHitDesc->m_shotForCenterMass ? " [Shot for center mass]\n" : "",
						   pHitDesc->m_helmetKnockedOff ? " [Helmet knocked off]\n" : "");

		// 		DumpActiveTasks(*pSb, true);
		// 		pSb->append_format("\n");

		addMailBody(*pSb);
	}

	if (const DoutMem* pMsgHistory = MsgGetHistoryMem())
	{
		ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);
		const U32F kBufferSize = 2 * 1024;
		char* pMsgBuffer	   = NDI_NEW char[kBufferSize];
		const U32F histSize	   = MsgGetHistory(pMsgBuffer, kBufferSize);
		addMailAttachment(pMsgBuffer, histSize, "tty.txt");
	}

	if (const DoutMemChannels* pDebugLog = pNavChar->GetChannelDebugLog())
	{
		ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);
		const U32F kBufferSize = 2 * 1024;

		char* pBuffer	   = NDI_NEW char[kBufferSize];
		const U32F logSize = pDebugLog->DumpChannelToBuffer(pBuffer,
															kBufferSize,
															kNpcLogChannelCombatDetails,
															AI::GetNpcLogChannelName,
															true);

		addMailAttachment(pBuffer, logSize, StringBuilder<256>("%s-log.txt", pNavChar->GetName()).c_str());
	}

#if 0
	Memory::Allocator* pStAlloc = Memory::GetAllocator(kAllocScopedTemp);
	if (pStAlloc && pStAlloc->CanAllocate(2*1024*1024, kAlign16))
	{
		ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);
		U8* pMem = NDI_NEW U8[1024*1024];

		const Maybe<CapturedScreenshot> sshot = Memory::IsDebugMemoryAvailable() ? CaptureScreenshot(pMem, 512*1024) : MAYBE::kNothing;

		char* pContentId = nullptr;
		if (sshot.Valid())
		{
			addMailAttachment(sshot.Get().m_outData, sshot.Get().m_outSize, "screenshot.jpg", kMailContentTypeImageJpeg, &pContentId);
		}
	}
#endif

	endMail();
}
#endif
