/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */


#include "gamelib/gameplay/health-system.h"

#include "gamelib/scriptx/h/nd-script-func-defines.h"
#include "gamelib/state-script/ss-manager.h"
#include "gamelib/stats/event-log.h"

#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-options.h"
#include "ndlib/net/nd-net-info.h"
#include "ndlib/process/clock.h"
#include "ndlib/process/process.h"


IHealthSystem::~IHealthSystem()
{
}

//--------------------------------------------------------------------------------------
// Class Health.
//--------------------------------------------------------------------------------------
class HealthSystem : public IHealthSystem
{
public:
	HealthSystem()
		: m_damageFromDirection(0, 0, 0)
		, m_regenRate(0.0f)
		, m_lastAttackHealth(0)
		, m_previousHealth(0)
		, m_partialHealthFragment(0.0f)
		, m_maxHealth(100)
		, m_upgradedHealth(0)
		, m_buffMaxHealth(0)
		, m_maxExplosionDamage(-1)
		, m_damageFromTimer(0.0f)
		, m_invincibility(DC::kInvincibilityNone)
		, m_explosionInvincibility(true)
		, m_overrideInvincibilityEndFrame(0)
		, m_lastStoppage(0.0f)
		, m_maxStoppage(0.0f)
		, m_stoppageRegenRate(0.0f)
		, m_damageScale(1.f)
		, m_protoDamageMultiplier(1.f)
		, m_invincibilityThreshold(1)
		, m_alwaysRegenInMelee(false)
		, m_allowOneShotProtection(true)
		, m_lastAttackedTime(Seconds(0))
		, m_bonusHealth(0)
		, m_iFramesCounter(0)
		, m_finalHitInvincibilityTimeOverride(-1)
		, m_finalHitCooldownTimeOverride(-1)
		, m_finalHitProtectionAllowed(true)
	{
		for (int i = 0; i < DC::kAiInjuryLevelCount; i++)
		{
			m_injuredHealth[i] = -1;
		}

		// Start with some positive time since last attack
		m_lastAttackedTimePd = GetClock()->GetCurTime() - Seconds(1000.0f);
	}

	bool				OverrideValid() const {return EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused <= m_overrideInvincibilityEndFrame;}
	DC::Invincibility	CurrInvincibility() const {return OverrideValid() ? m_overrideInvincibility : m_invincibility;}
	bool				CurrExplosionInvincibility() const {return OverrideValid() ? m_overrideExplosionInvincibility : m_explosionInvincibility;}
	I32					CurrMaxExplosionDamage() const {return OverrideValid() ? m_overrideMaxExplosionDamage : m_maxExplosionDamage;}


	virtual const Clock* GetClock() const override
	{
		if ( g_ndConfig.m_pNetInfo->IsNetActive())
		{
			return EngineComponents::GetNdFrameState()->GetClock(kNetworkClock);
		}
		return GetProcessClock();
	}

	virtual void SetMaxHealth(I32F maxHealth) override { m_maxHealth = maxHealth; }
	virtual void GiveUpgradeHealth(I32F extraHealth) override
	{
		m_upgradedHealth += extraHealth;
		m_lastAttackHealth = Min(m_lastAttackHealth + extraHealth, GetMaxHealth());
	}
	virtual void RemoveUpgradeHealth(I32F extraHealth) override
	{
		m_upgradedHealth = Max(m_upgradedHealth - extraHealth, 0);
		m_lastAttackHealth = Min(m_lastAttackHealth, GetMaxHealth());
	}

	virtual void SetBuffMaxHealth(I32F buffMaxHealth) override
	{
		I32F additionalHealth = buffMaxHealth - m_buffMaxHealth;
		m_buffMaxHealth = buffMaxHealth;
		if (additionalHealth > 0)
			SetHealth(GetHealth() + additionalHealth);
	}
	virtual void SetRegenRate(F32 regenRate) override
	{
		m_regenRate = regenRate;
	}
	virtual void SetRegenDelay(F32 regenDelay) override
	{
		if (regenDelay < m_regenDelay)
		{
			m_regenDelay = regenDelay;
		}
		else
		{
			// if new regen delay is longer, only update existing regen delay if we haven't healed
			// or started healing
			if (ToSeconds(GetClock()->GetTimePassed(m_lastAttackedTime)) - m_regenDelay < 0.0f)
			{
				m_lastAttackedTime = TimeFrameZero();
			}

			m_regenDelay = regenDelay;
		}
	}

	virtual I32F GetRawMaxHealth() const override { return m_maxHealth; }
	virtual I32F GetMaxHealth() const override { return m_maxHealth + m_upgradedHealth + m_buffMaxHealth; }
	virtual I32F GetUpgradedHealth() const override { return m_upgradedHealth; }

	virtual float GetRegenRate() const override { return m_regenRate; }
	virtual float GetRegenDelay() const override { return m_regenDelay; }

	virtual DC::Invincibility GetInvincibility(bool isExplosion = false) const override
	{
		if (isExplosion && !CurrExplosionInvincibility())
			return DC::kInvincibilityNone;
		else
			return CurrInvincibility();
	}
	virtual bool HasExplosionInvincibility() const override { return CurrExplosionInvincibility(); }
	virtual void SetInvincibility(DC::Invincibility invincibility, bool explosionInvincibility = true) override
	{
		m_invincibility = invincibility;
		m_explosionInvincibility = explosionInvincibility;

		if (invincibility == DC::kInvincibilityFull && explosionInvincibility)
			SetMaxExplosionDamage(0);
		else
			SetMaxExplosionDamage(-1);

		m_invincibilityThreshold = 1;
	}

	virtual void SetInvincibilityThreshold(I32 threshold) override
	{
		m_invincibilityThreshold = threshold;
	}

	virtual void OverrideInvincibilityThisFrame(DC::Invincibility invincibility, bool explosionInvincibility) override
	{
		m_overrideInvincibilityEndFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused + 1;
		m_overrideInvincibility = invincibility;
		m_overrideExplosionInvincibility = explosionInvincibility;

		if (invincibility == DC::kInvincibilityFull && explosionInvincibility)
			m_overrideMaxExplosionDamage = 0;
		else
			m_overrideMaxExplosionDamage = -1;
	}

	virtual float GetMaxStoppage() const override { return m_maxStoppage; }
	virtual void SetMaxStoppage(float maxStoppage) override { m_maxStoppage = maxStoppage; }
	virtual void SetStoppageRegenRate(float regenRate) override { m_stoppageRegenRate = regenRate; }
	virtual void SetStoppageRegenDelay(TimeFrame delay) override { m_stoppageRegenDelay = ToSeconds(delay); }

	virtual void Init(const HealthSettings& healthSettings) override
	{
		OverrideHealthSettings(healthSettings);

		m_lastStoppage = 0.0f;
		m_lastStoppedTime = GetClock()->GetCurTime();

		m_damageScale = 1.f;
		m_protoDamageMultiplier = 1.f;

		m_injuredHealth[DC::kAiInjuryLevelNone] = -1;
		m_injuredHealth[DC::kAiInjuryLevelMild] = healthSettings.m_mildInjuryHealth;
		m_injuredHealth[DC::kAiInjuryLevelCritical] = healthSettings.m_criticalInjuryHealth;

		m_lastPoiseTime = m_lastAdrenalineTime = GetClock()->GetCurTime();
		m_lastPoiseDamage = 0.0f;

		m_lastAttackHealth = healthSettings.m_maxHealth;
	}

	virtual void OverrideHealthSettings(const HealthSettings& healthSettings) override
	{
		m_maxHealth = healthSettings.m_maxHealth;
		m_buffMaxHealth = healthSettings.m_buffMaxHealth;
		m_maxExplosionDamage = healthSettings.m_maxExplosionDamage;
		float newRegenRate = healthSettings.m_healthRegenRate * (healthSettings.m_playerUltraEasy ? 2.0f : 1.0f);
		float newRegenDelay = healthSettings.m_healthRegenDelay * (healthSettings.m_playerUltraEasy ? 0.5f : 1.0f);

		float timePassed = ToSeconds(GetClock()->GetCurTime() - m_lastAttackedTime);
		float newTimePassed = (m_regenRate / newRegenRate) * (timePassed - m_regenDelay) + newRegenDelay;

		m_lastAttackedTime -= Seconds(newTimePassed - timePassed);
		m_regenRate = newRegenRate;
		m_regenDelay = newRegenDelay;

		m_stoppageRegenDelay = healthSettings.m_stoppageRegenDelay;
		m_maxStoppage = healthSettings.m_maxStoppage;
		m_stoppageRegenRate = healthSettings.m_stoppageRegenRate;

		m_finalHitInvincibilityTime = healthSettings.m_finalHitInvincibilityTime;
		m_finalHitCooldownTime = healthSettings.m_finalHitCooldownTime;
		m_lastAdrenaline = healthSettings.m_startAdrenaline;
		m_maxAdrenaline = healthSettings.m_maxAdrenaline;
		m_adrenalineRegenRate = healthSettings.m_adrenalineRegenRate;
		m_adrenalineRegenDelay = healthSettings.m_adrenalineRegenDelay;

		m_lastStamina = healthSettings.m_maxStamina;
		m_maxStamina = healthSettings.m_maxStamina;
		m_staminaRegenRate = healthSettings.m_staminaRegenRate;
		m_staminaRegenDelay = healthSettings.m_staminaRegenDelay;

		m_lastPoise = healthSettings.m_maxPoise;
		m_maxPoise = healthSettings.m_maxPoise;
		m_poiseRegenRate = healthSettings.m_poiseRegenRate;
		m_poiseRegenDelay = healthSettings.m_poiseRegenDelay;

		m_alwaysRegenInMelee = healthSettings.m_alwaysRegenInMelee;
		m_allowOneShotProtection = healthSettings.m_allowOneShotProtection;
	}

	virtual void SetHealth(I32F health) override
	{
		health = Min(health, GetMaxHealth());

		m_previousHealth = GetHealth();
		if (health < m_previousHealth)
		{
			m_lastAttackedTime = GetClock()->GetCurTime();
		}
		else if (health > m_previousHealth)
		{
			m_lastHealedTime = GetClock()->GetCurTime();
			m_finalHitProtectionAllowed = true;
		}

		m_lastAttackHealth = health;
		m_partialHealthFragment = 0.0f;
	}

	virtual void SetLastAttackHealthAndTime(I32 health, TimeFrame time) override
	{
		m_lastAttackHealth = health;
		m_lastAttackedTime = time;
		m_partialHealthFragment = 0.0f;
	}

	virtual I32F GetHealth() const override
	{
		I32 health = m_lastAttackHealth;
		I32 maxHealth = GetMaxHealth();
		if (health < maxHealth && health > 0 && m_regenRate > 0.0f)
		{
			const Clock* pClock = GetClock();
			F32 timePassed = ToSeconds(pClock->GetTimePassed(m_lastAttackedTime)) - m_regenDelay;
			if (timePassed > 0.0f)
			{
				F32 regenHealth = (m_regenRate * timePassed);
				I32 intHealth = I32(regenHealth);
				health += intHealth;
			}
		}

		return Min(health, maxHealth);
	}

	virtual F32 GetHealthAsFloat() const override
	{
		return F32(GetHealth()) + m_partialHealthFragment;
	}

	virtual I32F GetPreviousHealth() const override
	{
		return m_previousHealth;
	}

	virtual F32 GetPreviousHealthPercentage() const override
	{
		return m_previousHealth / (F32)GetMaxHealth();
	}

	virtual bool IsInjured(DC::AiInjuryLevel level) const override
	{
		ASSERT(level < DC::kAiInjuryLevelCount);
		float health = GetHealth();
		return (health < m_injuredHealth[level]); // IMPORTANT: (0.0 == injury DISABLED), (1.0 == injured whenever BELOW full health)
	}

	virtual DC::AiInjuryLevel GetCurInjuryLevel() const override
	{
		const float health = GetHealth();

		for (int i = DC::kAiInjuryLevelCount - 1; i > DC::kAiInjuryLevelNone; i--)
		{
			if (m_injuredHealth[i] > 0 && health <= m_injuredHealth[i])
			{
				return i;
			}
		}

		return DC::kAiInjuryLevelNone;
	}

	virtual bool IsRegeningHealth() const override
	{
		const Clock* pClock = GetClock();
		return (GetHealth() < GetMaxHealth()) && (ToSeconds(pClock->GetTimePassed(m_lastAttackedTime)) - m_regenDelay > 0.0f);
	}

	virtual float GetStoppage() const override
	{
		F32 stoppage = m_lastStoppage;
		if (stoppage > 0.0f && m_stoppageRegenRate > 0.0f)
		{
			const Clock* pClock = GetClock();
			F32 timePassed = ToSeconds(pClock->GetTimePassed(m_lastStoppedTime)) - m_stoppageRegenDelay;
			if (timePassed > 0.0f)
			{
				stoppage -= m_stoppageRegenRate *  timePassed;
				stoppage = Max(0.0f, stoppage);
			}
		}
		return stoppage;
	}

	virtual void DealDamage(F32 damage, bool allowOneShotKillProtection, bool isExplosion, bool ignoreInvincibility, bool isBullet) override
	{
		const DC::Invincibility curInvincibility = CurrInvincibility();
		const bool explosionInvincible = CurrExplosionInvincibility();
		if (curInvincibility == DC::kInvincibilityFull && (explosionInvincible || !isExplosion) && !ignoreInvincibility)
			return;

		if (damage > 0)
		{
			m_lastAttackHealth = GetHealth();
			m_lastAttackedTime = GetClock()->GetCurTime();
			m_lastAttackedTimePd = GetClock()->GetCurTime();
		}

		const F32 finalHitInvincibilityTime = m_finalHitInvincibilityTimeOverride >= 0
											? m_finalHitInvincibilityTimeOverride
											: m_finalHitInvincibilityTime;
		const F32 finalHitCooldownTime = m_finalHitCooldownTimeOverride >= 0
									   ? m_finalHitCooldownTimeOverride
									   : m_finalHitCooldownTime;

		// If we are still invincible from losing all our health, don't do any damage or keep track of it
		if (!m_finalHitInvincibility.Elapsed() && allowOneShotKillProtection && !ignoreInvincibility && finalHitInvincibilityTime > 0.0f)
		{
			return;
		}

		if (m_bonusHealth)
		{
			m_bonusHealth -= damage * m_damageScale * m_protoDamageMultiplier;
			if (m_bonusHealth <= 0)
			{
				// Cut through bonus into regular health
				damage = -m_bonusHealth / (m_damageScale * m_protoDamageMultiplier);
				m_bonusHealth = 0;
			}
			else
			{
				// Still have bonus health, don't affect anything else
				return;
			}
		}

		m_previousHealth = GetHealth();

		F32 afterHealthFloat = F32(m_previousHealth) + m_partialHealthFragment - (damage * m_damageScale * m_protoDamageMultiplier);

		I32 afterHealthInt = (I32) afterHealthFloat;
		m_partialHealthFragment = Max(0.0f, afterHealthFloat - F32(afterHealthInt));

		ASSERT(afterHealthInt <= m_previousHealth);  // damage should never increase health!

		m_lastAttackHealth = Max(afterHealthInt, I32(0));

		if (curInvincibility == DC::kInvincibilityCannotDie && (CurrExplosionInvincibility() || !isExplosion) && !ignoreInvincibility)
		{
			m_lastAttackHealth = Max(m_lastAttackHealth, m_invincibilityThreshold);
		}

		if (curInvincibility == DC::kInvincibilityCannotDieFromBullets && isBullet && !ignoreInvincibility)
		{
			m_lastAttackHealth = Max(m_lastAttackHealth, m_invincibilityThreshold);
		}

		if (m_lastAttackHealth == 0)
		{
			if (!g_ndConfig.m_pNetInfo->IsNetActive()
				// && m_previousHealth > 1
				&& (!m_finalHitCooldown.IsValid() || m_finalHitCooldown.Elapsed())
				&& allowOneShotKillProtection
				&& m_allowOneShotProtection
				&& m_finalHitProtectionAllowed)
			{
				g_eventLog.LogEvent("survive-with-one-health");
				g_ssMgr.BroadcastEvent(SID("player-survived-with-one-health"));
				m_lastAttackHealth = 1;
				m_finalHitCooldown.Set(finalHitCooldownTime);
				m_finalHitInvincibility.Set(finalHitInvincibilityTime);
				m_finalHitProtectionAllowed = false;
			}
		}
	}

	virtual void SetOneHitKillProtectionOverrideTimes(const F32 finalHitInvincibilityTimeOverride,
													  const F32 finalHitCooldownTimeOverride) override
	{
		m_finalHitCooldownTimeOverride = finalHitCooldownTimeOverride;
		m_finalHitInvincibilityTimeOverride = finalHitInvincibilityTimeOverride;
	}

	virtual void GiveOneHitKillProtection(bool give) override
	{
		m_allowOneShotProtection = give;
	}

	virtual bool DeadlyDamage(F32 damage, bool allowOneShotKillProtection/* = false*/, bool isExplosion/* = false*/, bool isBullet /* = false*/) const override
	{
		if (CurrInvincibility() == DC::kInvincibilityFull && (CurrExplosionInvincibility() || !isExplosion))
		{
			return false;
		}

		// Anything less than 1.0 is considered dead
		const F32 finalDamage = damage * m_damageScale * m_protoDamageMultiplier;
		const F32 curHealth = F32(GetHealth() + m_bonusHealth) + m_partialHealthFragment;
		const F32 healthRemaining = curHealth - finalDamage;
		if (healthRemaining >= 1.0f)
		{
			return false; // not enough to get to 0
		}

		if (CurrInvincibility() == DC::kInvincibilityCannotDie && (CurrExplosionInvincibility() || !isExplosion))
		{
			if (m_invincibilityThreshold > 0)
			{
				return false; // we would apply this health and it is > 0
			}
		}

		if (CurrInvincibility() == DC::kInvincibilityCannotDieFromBullets && isBullet)
		{
			return false;
		}

		// check final hit protection cooldown
		if (!g_ndConfig.m_pNetInfo->IsNetActive() && GetHealth() > 0 && (!m_finalHitCooldown.IsValid() || m_finalHitCooldown.Elapsed()) && allowOneShotKillProtection)
		{
			// the timer will start on this damage, so it won't be a kill
			return false;
		}

		if (!m_finalHitInvincibility.Elapsed() && allowOneShotKillProtection)
		{
			// the timer has started but has not elapsed yet
			return false;
		}

		return true;
	}

	virtual bool IsDead() const override
	{
		return GetHealth() <= 0;
	}

	virtual void Kill() override
	{
		SetHealth(0);
	}

	virtual void SetMaxExplosionDamage(I32F max) override
	{
		m_maxExplosionDamage = max;
	}

	virtual I32F GetMaxExplosionDamage() const override
	{
		I32 maxExplosionDamage = CurrMaxExplosionDamage();
		if (maxExplosionDamage >= 0)
		{
			return maxExplosionDamage;
		}

		return GetMaxHealth();
	}

	virtual float GetHealthPercentage() const override
	{
		ASSERT(GetMaxHealth() > 0.0f);
		if (GetMaxHealth() > 0.0f)
		{
			return (float)GetHealth() / (float)GetMaxHealth();
		}
		else
		{
			return 0.0f;
		}
	}

	virtual float GetScreenEffectHealthPercentage() const override
	{
		float health = (float)m_lastAttackHealth;
		float maxHealth = (float)GetMaxHealth();
		if (health < maxHealth && health > 0 && m_regenRate > 0.0f)
		{
			const Clock* pClock = GetClock();
			F32 timePassed = ToSeconds(pClock->GetTimePassed(m_lastAttackedTime)) - m_regenDelay;
			if (timePassed > 0.0f)
			{
				health += m_regenRate * timePassed;
				health = Min(health, maxHealth);
			}
		}

		return health / maxHealth;
	}

	virtual void SetDamageFromDirection(Vector_arg damageFromDirection, float timeValidInSeconds /* = 10.0f */) override
	{
		m_damageFromTimer = timeValidInSeconds;
		m_damageFromDirection = damageFromDirection;
	}

	virtual bool GetDamageFromDirection(Vector& damageFromDirection) const override
	{
		damageFromDirection = m_damageFromDirection;
		return m_damageFromTimer > 0;
	}

	virtual void DealStoppage(float stoppage) override
	{
		m_lastStoppage = GetStoppage() + stoppage;
		m_lastStoppage = Min(m_lastStoppage, m_maxStoppage);
		m_lastStoppedTime = GetClock()->GetCurTime();
	}

	virtual TimeFrame GetLastAttackTime() const override
	{
		return m_lastAttackedTime;
	}

	virtual TimeFrame GetTimeSinceLastAttacked() const override
	{
		return GetClock()->GetTimePassed(m_lastAttackedTime);
	}

	virtual TimeFrame GetTimeSinceLastAttackedPd() const override
	{
		return GetClock()->GetTimePassed(m_lastAttackedTimePd);
	}

	virtual TimeFrame GetLastHealedTime() const
	{
		return m_lastHealedTime;
	}

	virtual TimeFrame GetTimeSinceLastHealed() const override
	{
		return GetClock()->GetTimePassed(m_lastHealedTime);
	}


	// Proto Damge Multiplier
	virtual F32 GetProtoDamageMultiplier() const override { return m_protoDamageMultiplier; }
	virtual void SetProtoDamageMultiplier(F32 multipiler) override { m_protoDamageMultiplier = multipiler; }

	virtual void SetDamageScale(F32 damageScale) override
	{
		m_damageScale = damageScale;
	}

	virtual F32 GetAdrenaline() const override
	{
		F32 stamina = m_lastAdrenaline;

		if (stamina < m_maxAdrenaline && m_adrenalineRegenRate > 0.0f)
		{
			const Clock* pClock = GetClock();
			F32 timePassed = ToSeconds(pClock->GetTimePassed(m_lastAdrenalineTime)) - m_adrenalineRegenDelay;
			if (timePassed > 0.0f)
			{
				stamina += m_adrenalineRegenRate * timePassed;
			}
		}

		return Min(stamina, m_maxAdrenaline);
	}

	virtual F32 GetMaxAdrenaline() const override
	{
		return m_maxAdrenaline;
	}

	virtual F32 GetAdrenalinePercentage() const override
	{
		if (m_maxAdrenaline > 0.0f)
		{
			return Max(GetAdrenaline(), 0.0f) / m_maxAdrenaline;
		}
		return 0.0f;
	}

	virtual void UseAdrenaline(F32 stamina) override
	{
		if (stamina > 0.0f)
		{
			F32 curStamina = GetAdrenaline();
			m_lastAdrenaline = Max(0.0f, curStamina - stamina);
			m_lastAdrenalineTime = GetClock()->GetCurTime();
		}
	}

	virtual void GiveAdrenaline(F32 stamina) override
	{
		if (stamina != 0.0f)
		{
			F32 curStamina = GetAdrenaline();
			m_lastAdrenaline = Min(GetMaxAdrenaline(), curStamina + stamina);
			m_lastAdrenaline = Max(m_lastAdrenaline, 0.0f);
			m_lastAdrenalineTime = GetClock()->GetCurTime();
		}
	}

	virtual void SetAdrenaline(F32 stamina) override
	{
		m_lastAdrenaline = MinMax(stamina, 0.0f, m_maxAdrenaline);
		m_lastAdrenalineTime = GetClock()->GetCurTime();
	}

	virtual void PauseAdrenaline() override
	{
		F32 curStamina = GetAdrenaline();
		m_lastAdrenaline = Max(0.0f, curStamina);
		m_lastAdrenalineTime = GetClock()->GetCurTime();
	}

	virtual void ResetAdrenaline() override
	{
		m_lastAdrenaline = GetMaxAdrenaline();
		m_lastAdrenalineTime = GetClock()->GetCurTime();
	}

	virtual F32 GetStamina() const override
	{
		F32 stamina = m_lastStamina;

		if (stamina < m_maxStamina && m_staminaRegenRate > 0.0f)
		{
			const Clock* pClock = GetClock();
			F32 timePassed = ToSeconds(pClock->GetTimePassed(m_lastStaminaTime)) - m_staminaRegenDelay;
			if (timePassed > 0.0f)
			{
				stamina += m_staminaRegenRate * timePassed;
			}
		}

		return Min(stamina, m_maxStamina);
	}

	virtual F32 GetMaxStamina() const override
	{
		return m_maxStamina;
	}

	virtual F32 GetStaminaPercentage() const override
	{
		if (m_maxStamina > 0.0f)
		{
			return Max(GetStamina(), 0.0f) / m_maxStamina;
		}
		return 0.0f;
	}

	virtual void UseStamina(F32 stamina) override
	{
		F32 curStamina = GetStamina();
		m_lastStamina = Max(0.0f, curStamina - stamina);
		m_lastStaminaTime = GetClock()->GetCurTime();
	}

	virtual void ResetStamina() override
	{
		m_lastStamina = GetMaxStamina();
		m_lastStaminaTime = GetClock()->GetCurTime();
	}

	virtual bool GetAlwaysRegenInMelee() const override { return m_alwaysRegenInMelee; }


	virtual void StartHealthRegen() override
	{
		const F32 timePassed = ToSeconds(GetClock()->GetTimePassed(m_lastAttackedTime)) - m_regenDelay;
		if (timePassed < 0.0f)
		{
			m_lastAttackedTime += Seconds(timePassed);
		}
	}

	virtual void SupressHealthRegen() override
	{
		SetHealth(GetHealth());
		m_lastAttackedTime = GetClock()->GetCurTime();
	}

	// I-Frames
	virtual void GiveIFrames(TimeFrame iFrames) override
	{
		TimeFrame newTime = GetClock()->GetCurTime() + iFrames;
		if (newTime > m_iFrameEndTime)
			m_iFrameEndTime = newTime;

		m_iFramesCounter = 2;
	}

	virtual void RemoveIFrames() override
	{
		m_iFrameEndTime = Seconds(0.0f);

		m_iFramesCounter = 0;
	}

	virtual bool InIFrame() const override
	{
		return (GetClock()->GetCurTime() < m_iFrameEndTime) || m_iFramesCounter > 0;
	}

	virtual void SetIFrameUsedTime() override
	{
		m_iFrameUsedTime = GetClock()->GetCurTime();
	}

	virtual F32 GetIFrameUsedTime() const override
	{
		return GetClock()->GetTimePassed(m_iFrameUsedTime).ToSeconds();
	}

	virtual void UpdateIFrames() override
	{
		if (m_iFramesCounter > 0)
		{
			if (GetClock()->GetCurTime() > m_iFrameEndTime)
			{
				m_iFramesCounter--;
			}
		}
	}

	virtual void GiveCoreFrames(TimeFrame iFrames) override
	{
		m_coreFrameEndTime = GetClock()->GetCurTime() + iFrames;
	}

	virtual bool InCoreFrame() const override
	{
		return (GetClock()->GetCurTime() < m_coreFrameEndTime);
	}


	// Poise
	virtual void GiveHyperArmor(TimeFrame iFrames) override
	{
		m_hyperArmorEndTime = GetClock()->GetCurTime() + iFrames;
	}

	virtual void DealPoiseDamage(F32 poise) override
	{
		F32 curPoise = GetPoise();
		m_lastPoise = Max(0.0f, curPoise - poise);
		m_lastPoiseTime = GetClock()->GetCurTime();
	}

	virtual void ResetPoise() override
	{
		m_lastPoise = m_maxPoise;
		m_lastPoiseTime = GetClock()->GetCurTime();
	}

	virtual void SetLastPoiseDamage(F32 damage) override
	{
		m_lastPoiseDamage = damage;
	}

	virtual F32 GetLastPoiseDamage() const override
	{
		return m_lastPoiseDamage + 1;
	}

	virtual bool HasHyperArmor() const override
	{
		if (InIFrame())
			return true;

		if (GetClock()->GetCurTime() < m_hyperArmorEndTime)
		{
			return true;
		}

		return false;
	}

	virtual F32 GetPoise() const override
	{
		if (GetClock()->GetCurTime() < m_hyperArmorEndTime)
		{
			return 10000.0f;
		}

		return m_lastPoise;

		/*F32 poise = m_lastPoise;

		if (poise < m_maxPoise && m_poiseRegenRate > 0.0f)
		{
			const Clock* pClock = GetClock();
			F32 timePassed = ToSeconds(pClock->GetTimePassed(m_lastPoiseTime)) - m_poiseRegenDelay;
			if (timePassed > 0.0f)
			{
				poise += m_poiseRegenRate * timePassed;
			}
		}

		return Min(poise, m_maxPoise);*/
	}

	virtual I32 GetBonusHealth() const override
	{
		return m_bonusHealth;
	}

	virtual void GiveBonusHealth(I32 bonus) override
	{
		m_bonusHealth = bonus;
	}

private:
	Vector				m_damageFromDirection;
	TimeFrame			m_lastAttackedTime;			//!< Last time we got attacked
	TimeFrame			m_lastAttackedTimePd;		//!< Last time we got attacked (temp, simplified version for permadeath)
	TimeFrame			m_lastHealedTime;			//!< Last time we were healed
	float				m_regenDelay;				//!< Time until after the last attack before starting regeneration
	float				m_regenRate;				//!< Amount of health units per second regenerated
	I32					m_lastAttackHealth;			//!< Amount of health when m_lastAttackedTime was set
	F32					m_partialHealthFragment;	//!< Tracks rounding error on health
	I32					m_previousHealth;

	I32					m_maxHealth;				//!< Maximum health points of the entity
	I32					m_upgradedHealth;			// for upgrades.
	I32					m_buffMaxHealth;
	I32					m_maxExplosionDamage;
	I32					m_injuredHealth[DC::kAiInjuryLevelCount];
	I32					m_bonusHealth;

	float				m_damageFromTimer;
	DC::Invincibility	m_invincibility;
	I32					m_invincibilityThreshold;
	bool				m_explosionInvincibility;

	I64					m_overrideInvincibilityEndFrame;
	DC::Invincibility	m_overrideInvincibility;
	I32					m_overrideInvincibilityThreshold;
	bool				m_overrideExplosionInvincibility;
	I32					m_overrideMaxExplosionDamage;


	TimeFrame			m_lastStoppedTime;			//!< Last time we were stopped
	float				m_stoppageRegenDelay;		//!< Time until after attack before regenerating speed
	float				m_lastStoppage;				//!< Stoppage when m_lastStoppedTime was set
	float				m_maxStoppage;				//!< Maximum slow down percent
	float				m_stoppageRegenRate;		//!< Amount of stoppage units per second regenerated

	F32					m_damageScale;				//!< Acts as a multiplier on any damage done

	TimeFrame			m_lastAdrenalineTime;
	F32					m_lastAdrenaline;
	F32					m_maxAdrenaline;
	F32					m_adrenalineRegenRate;
	F32					m_adrenalineRegenDelay;

	TimeFrame			m_lastStaminaTime;
	F32					m_lastStamina;
	F32					m_maxStamina;
	F32					m_staminaRegenRate;
	F32					m_staminaRegenDelay;

	F32					m_finalHitInvincibilityTime;
	F32					m_finalHitInvincibilityTimeOverride;
	TimeStamp			m_finalHitInvincibility;
	F32					m_finalHitCooldownTime;
	F32					m_finalHitCooldownTimeOverride;
	TimeStamp			m_finalHitCooldown;
	bool				m_finalHitProtectionAllowed;

	bool				m_alwaysRegenInMelee;
	bool				m_allowOneShotProtection;

	TimeFrame			m_iFrameEndTime;
	TimeFrame			m_hyperArmorEndTime;
	TimeFrame			m_iFrameUsedTime;
	TimeFrame			m_coreFrameEndTime;
	I32					m_iFramesCounter;

	TimeFrame			m_lastPoiseTime;
	F32					m_lastPoise;
	F32					m_lastPoiseDamage;
	F32					m_maxPoise;
	F32					m_poiseRegenRate;
	F32					m_poiseRegenDelay;

	F32					m_protoDamageMultiplier;



};

IHealthSystem* CreateHealthSystem()
{
	return NDI_NEW HealthSystem;
}
