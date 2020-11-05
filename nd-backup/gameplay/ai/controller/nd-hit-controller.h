/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ND_AI_HIT_CONTROLLER_H
#define ND_AI_HIT_CONTROLLER_H

#include "gamelib/gameplay/ai/controller/animaction-controller.h"
#include "gamelib/gameplay/ai/nav-character-anim-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
struct INdAiHitController : public AnimActionController
{
	virtual ~INdAiHitController() override {}
	virtual void FadeOutHitLayers(F32 fadeTime = 0.1f) = 0;
	virtual void FadeOutNonDeathLayers(F32 fadeTime = 0.1f) = 0;
	virtual float GetGroundAdjustBlendFactor() const = 0;
	virtual float GetRagdollBlendOutHoleDepth() const = 0;
	virtual void NotifyDcRelocated(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) = 0;

	virtual void Die(const HitDescription& hitDesc, bool fromScript = false) = 0;
	virtual void DieSpecial(StringId64 deathAnim,
							const BoundFrame* pApOverride = nullptr,
							StringId64 customApRefId	  = INVALID_STRING_ID_64,
							float fadeTime = -1.0f) = 0;

	virtual void DieOnHorse() = 0;
	virtual bool DeathAnimWhitelistedForRagdollGroundProbesFix() const = 0;
};

#endif // ND_AI_HIT_CONTROLLER_H
