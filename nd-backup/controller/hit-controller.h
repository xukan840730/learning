/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/gameplay/ai/controller/nd-hit-controller.h"

#include "ndlib/process/process-handles.h"

#include "game/scriptx/h/hit-reactions-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class Character;
struct AttackInfo;

/// --------------------------------------------------------------------------------------------------------------- ///
struct HitDescription
{
	HitDescription()
	{
		memset(this, 0, sizeof(HitDescription));
		m_distToTarget = -1.0f;
		m_distToPlayer = -1.0f;
	}

	NdGameObjectHandle			m_hSourceGameObj;
	DC::HitReactionSourceType	m_sourceType;
	Vector						m_directionWs;
	Point						m_sourcePointWs;
	Point						m_impactPointWs;
	F32							m_prevNormalizedHealth;
	F32							m_normalizedHealth;
	F32							m_distToTarget;
	F32							m_distToPlayer;
	DC::HitReactionStateMask	m_npcState;
	DC::HitReactionLimbMask		m_limb;
	StringId64					m_hitAttachName;
	RigidBodyHandle				m_hHitBody;
	bool						m_helmetKnockedOff;
	bool						m_headShot;
	bool						m_shotForCenterMass;
	bool						m_isAdditiveMeleeAttack;
	bool						m_tryToFireBack;
	bool						m_stun;
	U32							m_damage;
	float						m_stoppingPower;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsHitReactionSourceTypeStun(const DC::HitReactionSourceType src)
{
	if (src
		& (DC::kHitReactionSourceTypeBulletStun | DC::kHitReactionSourceTypeThrownObject | DC::kHitReactionSourceTypeBuddyThrownObject
		   | DC::kHitReactionSourceTypeSmokeOrStunBomb | DC::kHitReactionSourceTypeExplosionWeak
		   | DC::kHitReactionSourceTypeExplosionStrong))
	{
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct IAiHitController : public INdAiHitController
{
	virtual ~IAiHitController() override {}

	virtual bool IsLocked() = 0;
	virtual void CancelAnimAction() = 0;

	virtual bool IsHitReactionPlaying() const = 0;
	virtual bool IsExplosionFullBodyReactionPlaying() const = 0;
	virtual bool IsFullBodyReactionPlaying(DC::HitReactionStateMask mask = 0) const = 0;
	virtual bool IsStunReactionPlaying() const = 0;
	virtual bool IsStunKneeReactionPlaying() const = 0;
	virtual bool IsStunReactionFromThrownObjectPlaying() const = 0;
	virtual bool IsStunReactionFromBuddyThrownObjectPlaying() const = 0;
	virtual bool IsStunReactionFromNonBuddyThrownObjectPlaying() const = 0;
	virtual bool IsStunReactionFromSmokeOrStunBombPlaying() const = 0;
	virtual bool IsStunReactionFromPlayerSmokeOrStunBombPlaying() const = 0;
	virtual TimeFrame GetLastStunPlayingTime() const = 0;
	virtual float GetSecInStunReaction() const		  = 0;
	virtual bool IsInStrikePhaseRange() const		  = 0;
	virtual bool IsAdditiveHitReactionPlaying() const = 0;
	virtual StringId64 GetAdditiveAnim() const = 0;
	virtual bool IsAdditiveHitReactionPausingMelee() const = 0;
	virtual StringId64 GetOverrideHitPartial() const = 0;
	virtual const TimeFrame GetLastHitTime() const = 0;
	virtual void SetLastHitTime(TimeFrame time) = 0;

	virtual void SetPartialOverride(StringId64 overridePartial, F32 frameStart = 0.0f) = 0;

	virtual bool IsFireBackGesturePlaying() const = 0;

	virtual bool Shove(Character* pShover, Point_arg originWs, Vector_arg motionDirWs) = 0;
	virtual bool Stun(float stunTime, DC::HitReactionSourceType hitReactionSourceType) = 0;
	virtual bool IsBeingShoved() const = 0;

	virtual bool GoLegless(bool leftLeg, const AttackInfo* pAttackInfo) = 0;

	static bool CheckForNpcCollision(NavCharacter* pStumblingCharacter);
	virtual const TimeFrame GetTimeSinceLastReaction() const = 0;
	virtual void OverrideNextHitDirectionWs(Point_arg hitDirTargetWs) = 0;

	virtual bool WasKnockedDown() const = 0;
	virtual void KnockDown() = 0;

	virtual bool CanInitiateMeleeAttack() const = 0;

	virtual void NetHitReaction(StringId64 animListId,
								StringId64 bucketId,
								U32 animIndex,
								F32 startPhase,
								F32 playbackRate,
								TimeFrame startTime,
								const BoundFrame& boundFrame) = 0;
	virtual void NetDeathReaction(StringId64 animListId,
								  StringId64 bucketId,
								  U32 animIndex,
								  F32 exitPhase,
								  const BoundFrame& boundFrame,
								  bool mirror) = 0;
	virtual TimeFrame GetLastNetReactTime() = 0;

	virtual StringId64 GetLastMeleeHitReactionAnim() = 0;

	virtual bool PlayManualAdditiveHR(const StringId64 animId, const float strength, const bool mirror) = 0;

	virtual void ValidateHitReactions() const = 0;

	virtual void PostDeathAnim(StringId64 anim, StringId64 ragdollSettings) = 0;

};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiHitController* CreateAiHitController();
