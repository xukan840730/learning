/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/ai/agent/nav-character-adapter.h"
#include "gamelib/gameplay/ai/nav-character-anim-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NavCharacter;
class SimpleNavCharacter;
class NavControl;
class NdAiAnimationConfig;
class NdGameObject;
class ScreenSpaceTextPrinter;
class SimpleNavControl;
struct HitDescription;

namespace DC
{
	struct NpcDemeanorDef;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimActionController
{
public:
	virtual ~AnimActionController() {}

	AnimActionController() : m_pCharacter(nullptr), m_pNavControl(nullptr) {}

	virtual void Init(NdGameObject* pCharacter, const SimpleNavControl* pNavControl);
	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl);

	virtual void Shutdown();
	virtual void Reset() {}	  // Reset to a working default state
	virtual void Interrupt(); // Interrupt the controller to prevent any further changes
	virtual void OnHitReactionPlayed() {}

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	virtual void RequestAnimations() {}
	virtual void UpdateStatus() = 0;
	virtual void UpdateProcedural() {}
	virtual void PostRootLocatorUpdate() {}
	virtual void PostRenderUpdate() {}

	virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const {}

	virtual bool IsBusy() const = 0;

	virtual bool ShouldInterruptNavigation() const { return false; }
	virtual bool ShouldInterruptSkills() const { return false; }
	virtual bool RequestAbortAction() { return false; }

	virtual U64 CollectHitReactionStateFlags() const { return 0; }

	virtual bool TakeHit(const HitDescription* pHitDesc) { return false; }

	virtual void ConfigureCharacter(Demeanor demeanor,
									const DC::NpcDemeanorDef* pDemeanorDef,
									const NdAiAnimationConfig* pAnimConfig)
	{
	}

	virtual void EnterNewParentSpace(const Transform& matOldToNew,
									 const Locator& oldParentSpace,
									 const Locator& newParentSpace)
	{
	}

	virtual bool IsNavAnimController() const { return false; }

protected:
	bool IsValid() const { return m_pCharacter != nullptr; }

	NavCharacter* GetCharacter() const;
	SimpleNavCharacter* GetSimpleNavCharacter() const;
	NdGameObject* GetCharacterGameObject() const;
	NavCharacterAdapter GetCharacterAdapter() const;

	const SimpleNavControl* GetSimpleNavControl() const;
	const NavControl* GetNavControl() const;

private:
	NdGameObject* m_pCharacter;
	const SimpleNavControl* m_pNavControl;
};
