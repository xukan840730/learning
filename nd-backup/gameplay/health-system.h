/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

/*! \file health.h
   \brief Health system class.
*/

#ifndef _ND_HEALTH_SYSTEM_H_
#define _ND_HEALTH_SYSTEM_H_

#include "corelib/util/timeframe.h"
#include "gamelib/scriptx/h/nd-script-func-defines.h"

class Clock;

//--------------------------------------------------------------------------------------
/// Class Health: Manage the life of a character.
//--------------------------------------------------------------------------------------

struct HealthSettings
{
	bool m_playerUltraEasy;
	I32 m_maxExplosionDamage;

	I32 m_startingHealth;
	I32 m_maxHealth;
	I32 m_buffMaxHealth;
	F32 m_healthRegenRate;
	F32 m_healthRegenDelay;

	I32 m_mildInjuryHealth;
	I32 m_criticalInjuryHealth;

	F32 m_maxStoppage;
	F32 m_stoppageRegenDelay;
	F32 m_stoppageRegenRate;

	F32 m_maxAdrenaline;
	F32 m_adrenalineRegenRate;
	F32 m_adrenalineRegenDelay;
	F32 m_startAdrenaline;

	F32 m_maxStamina;
	F32 m_staminaRegenRate;
	F32 m_staminaRegenDelay;


	F32 m_finalHitCooldownTime;
	F32 m_finalHitInvincibilityTime;

	bool m_alwaysRegenInMelee;
	bool m_allowOneShotProtection;

	// Poise
	F32 m_maxPoise;
	F32 m_poiseRegenRate;
	F32 m_poiseRegenDelay;

	HealthSettings()
		: m_playerUltraEasy(false)
		, m_maxExplosionDamage(100)
		, m_startingHealth(100)
		, m_maxHealth(100)
		, m_buffMaxHealth(0)
		, m_healthRegenRate(0.0f)
		, m_healthRegenDelay(0.0f)
		, m_mildInjuryHealth(-1)
		, m_criticalInjuryHealth(-1)
		, m_maxStoppage(0.0f)
		, m_stoppageRegenDelay(0.0f)
		, m_stoppageRegenRate(0.0f)
		, m_maxAdrenaline(100.0f)
		, m_startAdrenaline(100.0f)
		, m_adrenalineRegenRate(0.0f)
		, m_adrenalineRegenDelay(0.0f)
		, m_maxStamina(100.0f)
		, m_staminaRegenRate(0.0f)
		, m_staminaRegenDelay(0.0f)
		, m_finalHitInvincibilityTime(0.0f)
		, m_finalHitCooldownTime(0.0f)
		, m_alwaysRegenInMelee(false)
		, m_allowOneShotProtection(true)
		, m_maxPoise(100.0f)
		, m_poiseRegenDelay(0.0f)
		, m_poiseRegenRate(0.0f)
	{}
};

class IHealthSystem
{
public:
	virtual ~IHealthSystem();
	virtual void Init(const HealthSettings& healthSettings) = 0;
	virtual void OverrideHealthSettings(const HealthSettings& healthsettings) = 0;

	virtual void DealDamage(F32 damage, bool allowOneShotKillProtection = false, bool isExplosion = false, bool ignoreInvincibility = false, bool isBullet = false) = 0;
	virtual bool DeadlyDamage(F32 damage, bool allowOneShotKillProtection = false, bool isExplosion = false, bool isBullet = false) const = 0;
	virtual bool IsDead() const = 0;
	virtual void Kill() = 0;

	virtual I32F GetPreviousHealth() const = 0;
	virtual F32 GetPreviousHealthPercentage() const = 0;

	virtual bool IsInjured(DC::AiInjuryLevel level) const = 0;
	virtual DC::AiInjuryLevel GetCurInjuryLevel() const = 0;

	virtual void SetHealth(I32F health) = 0;
	virtual void SetMaxHealth(I32F maxHealth) = 0;
	virtual void GiveUpgradeHealth(I32F extraHealth) = 0;
	virtual void RemoveUpgradeHealth(I32F extraHealth) = 0;
	virtual void SetBuffMaxHealth(I32F buffMaxHealth) = 0;
	virtual void SetRegenRate(F32 regenRate) = 0;
	virtual void SetRegenDelay(F32 regenDelay) = 0;
	virtual void SetLastAttackHealthAndTime(I32 health, TimeFrame time) = 0;
	virtual TimeFrame GetLastAttackTime() const = 0;
	virtual TimeFrame GetTimeSinceLastAttacked() const = 0;
	virtual TimeFrame GetTimeSinceLastHealed() const = 0;

	// Hack, simplified, non buggy attack time for permadeath
	virtual TimeFrame GetTimeSinceLastAttackedPd() const = 0;

	virtual I32F GetHealth() const = 0;
	virtual F32 GetHealthAsFloat() const = 0;
	virtual I32F GetMaxHealth() const = 0;
	virtual I32F GetRawMaxHealth() const = 0;
	virtual I32F GetUpgradedHealth() const = 0;
	virtual float GetRegenRate() const = 0;
	virtual bool IsRegeningHealth() const = 0;
	virtual float GetRegenDelay() const = 0;

	virtual void SetMaxExplosionDamage(I32F max) = 0;
	virtual I32F GetMaxExplosionDamage() const = 0;

	virtual float GetHealthPercentage() const = 0;
	virtual float GetScreenEffectHealthPercentage() const = 0;

	/// Set and return the damage from direction vector.
	/// \param damageFromDirection the direction where the damage is coming from.
	virtual void SetDamageFromDirection(Vector_arg damageFromDirection, float timeValidInSeconds = 10.0f) = 0;
	virtual bool GetDamageFromDirection(Vector& damageFromDirection) const = 0;

	virtual DC::Invincibility GetInvincibility(bool isExplosion = false) const = 0;
	virtual void SetInvincibility(DC::Invincibility invincibility, bool explosionInvincibility = true) = 0;
	virtual void SetInvincibilityThreshold(I32 threshold) = 0;
	virtual void OverrideInvincibilityThisFrame(DC::Invincibility invincibility, bool explosionInvincibility = true) = 0;
	virtual bool HasExplosionInvincibility() const = 0;

	virtual void DealStoppage(float stoppage) = 0;
	virtual float GetStoppage() const = 0;
	virtual float GetMaxStoppage() const = 0;
	virtual void SetMaxStoppage(float maxStoppage) = 0;
	virtual void SetStoppageRegenRate(float regenRate) = 0;
	virtual void SetStoppageRegenDelay(TimeFrame delay) = 0;

	virtual void SetDamageScale(F32 damageScale) = 0;

	// Proto Damge Multiplier
	virtual F32 GetProtoDamageMultiplier() const = 0;
	virtual void SetProtoDamageMultiplier(F32 multipiler) = 0;

	virtual F32 GetAdrenaline() const = 0;
	virtual F32 GetMaxAdrenaline() const = 0;
	virtual void UseAdrenaline(F32 stamina) = 0;
	virtual F32 GetAdrenalinePercentage() const = 0;
	virtual void ResetAdrenaline() = 0;
	virtual void PauseAdrenaline() = 0;
	virtual void GiveAdrenaline(F32 adrenaline) = 0;
	virtual void SetAdrenaline(F32 adrenaline) = 0;

	virtual F32 GetStamina() const = 0;
	virtual F32 GetMaxStamina() const = 0;
	virtual void UseStamina(F32 stamina) = 0;
	virtual F32 GetStaminaPercentage() const = 0;
	virtual void ResetStamina() = 0;

	virtual void StartHealthRegen() = 0;
	virtual void SupressHealthRegen() = 0;

	virtual bool GetAlwaysRegenInMelee() const = 0;

	virtual const Clock* GetClock() const = 0;

	// I-Frames
	virtual void GiveIFrames(TimeFrame iFrameTime) = 0;
	virtual void RemoveIFrames() = 0;
	virtual bool InIFrame() const = 0;
	virtual void SetIFrameUsedTime() = 0;
	virtual F32 GetIFrameUsedTime() const = 0;
	virtual void GiveCoreFrames(TimeFrame iFrameTime) = 0;
	virtual bool InCoreFrame() const = 0;
	virtual void UpdateIFrames() = 0;

	// Poise
	virtual void DealPoiseDamage(F32 poise) = 0;
	virtual void ResetPoise() = 0;
	virtual F32 GetPoise() const = 0;
	virtual void SetLastPoiseDamage(F32 damage) = 0;
	virtual F32 GetLastPoiseDamage() const = 0;
	bool IsPoiseBroken() const { return GetPoise() < GetLastPoiseDamage(); }
	virtual void GiveHyperArmor(TimeFrame hyperArmorTime) = 0;
	virtual bool HasHyperArmor() const = 0;

	// Bonus Health
	virtual I32 GetBonusHealth() const = 0;
	virtual void GiveBonusHealth(I32 bonus) = 0;

	virtual void GiveOneHitKillProtection(bool give) = 0;
	virtual void SetOneHitKillProtectionOverrideTimes(const F32 finalHitInvincibilityTimeOverride = -1,
													  const F32 finalHitCooldownTimeOverride = -1) = 0;
};

IHealthSystem* CreateHealthSystem();

#endif // _ND_HEALTH_SYSTEM_H_
