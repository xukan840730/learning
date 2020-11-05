/*
 * Copyright (c) 2018 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-simple-instance.h"
#include "ndlib/anim/anim-state-instance.h"

#include "gamelib/level/art-item-anim.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSample
{
public:
	AnimSample() = default;
	AnimSample(ArtItemAnimHandle anim, float phase, bool mirror = false, float rate = 1.0f)
		: m_animHandle(anim)
		, m_phase(phase)
		, m_rate(rate)
		, m_mirror(mirror)
	{
		SetDbgNameId();
	}

	AnimSample(const AnimStateInstance* pInstance, bool useOriginal = false)
	{
		m_animHandle = pInstance
						   ? (useOriginal ? pInstance->GetOriginalPhaseAnimArtItem() : pInstance->GetPhaseAnimArtItem())
						   : ArtItemAnimHandle();

		if (const ArtItemAnim* pPhaseAnim = m_animHandle.ToArtItem())
		{
			m_phase	 = pInstance->GetPhase();
			m_rate	 = 1.0f;
			m_mirror = pInstance->IsFlipped();

			ANIM_ASSERTF(m_phase >= 0.0f && m_phase <= 1.0f,
						 ("Anim State Instance '%s' (anim: '%s') has invalid phase %f",
						  DevKitOnly_StringIdToString(pInstance->GetStateName()),
						  pPhaseAnim->GetName(),
						  m_phase));
		}
		else
		{
			m_phase	 = 0.0f;
			m_mirror = false;
			m_rate	 = 1.0f;
		}

		SetDbgNameId();
	}

	AnimSample(const AnimSimpleInstance* pInstance)
	{
		m_animHandle = pInstance ? pInstance->GetAnim() : ArtItemAnimHandle();

		if (const ArtItemAnim* pAnim = m_animHandle.ToArtItem())
		{
			m_phase	 = pInstance->GetPhase();
			m_rate	 = 1.0f;
			m_mirror = pInstance->IsFlipped();

			ANIM_ASSERTF(m_phase >= 0.0f && m_phase <= 1.0f,
						 ("Anim Simple Instance '%s' has invalid phase %f",
						  pAnim->GetName(),
						  m_phase));
		}
		else
		{
			m_phase	 = 0.0f;
			m_mirror = false;
			m_rate	 = 1.0f;
		}

		SetDbgNameId();
	}

	ArtItemAnimHandle Anim() const { return m_animHandle; }

	StringId64 GetAnimNameId() const;
	StringId64 GetDbgNameId() const { return m_dbgNameId; }

	float Phase() const { return m_phase; }
	float Frame() const;
	float Sample() const;
	bool Mirror() const { return m_mirror; }
	float Rate() const { return m_rate; }

	void SetPhase(float p) { m_phase = p; }
	U32F Advance(TimeFrame time);
	void RoundToNearestFrame();

private:

	void SetDbgNameId()
	{
		const ArtItemAnim* pAnim = m_animHandle.ToArtItem();
		m_dbgNameId = pAnim ? pAnim->GetNameId() : SID("???");
	}

	ArtItemAnimHandle m_animHandle;
	float m_phase = 0.0f;
	float m_rate = 1.0f;
	bool m_mirror = false;

	StringId64 m_dbgNameId = INVALID_STRING_ID_64; // used for tracking down player freeze issue.
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSampleBiased : public AnimSample
{
public:
	AnimSampleBiased() = default;
	AnimSampleBiased(const AnimSample& sample, float costBias) : AnimSample(sample), m_costBias(costBias) {}

	float CostBias() const { return m_costBias; }

private:
	float m_costBias = 0.0f;
};
