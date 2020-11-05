/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/nav/action-pack.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ActionPackController;
class AnimActionController;
class Demeanor;
class INavAnimController;
class INdAiCinematicController;
class INdAiClimbController;
class INdAiLocomotionController;
class INdAiTraversalController;
class INdAiWeaponController;
class NavCharacter;
class NavControl;
class NdAiAnimationConfig;
class NdGameObject;
class ScreenSpaceTextPrinter;
class SimpleNavControl;
class AiEntryController;
struct BitArray128;
struct INdAiHitController;

namespace DC
{
	struct NpcDemeanorDef;
}

/// --------------------------------------------------------------------------------------------------------------- ///
enum BusyExcludeFlags
{
	kExcludeNone	= 0,
	kExcludeFades	= (1 << 0)
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NdAnimationControllers
{
public:
	NdAnimationControllers(U32 numControllers);
	virtual ~NdAnimationControllers();

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	void Init(NavCharacter* pNavChar, const NavControl* pNavControl);
	void Init(NdGameObject* pNavChar, const SimpleNavControl* pNavControl);
	void Destroy();
	void Reset();
	void ResetExcluding(const BitArray128& bitArrayExclude);
	void InterruptNavControllers(const INavAnimController* pIgnore = nullptr);
	void ResetNavigation(const INavAnimController* pNewActiveNavAnimController, bool playIdle = true);

	void RequestAnimations();
	void UpdateStatus();
	void UpdateProcedural();
	void PostRenderUpdate();
	void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const;
	bool IsBusy() const;
	bool IsBusy(const BitArray128& bitArrayInclude) const;
	bool IsBusyExcluding(const BitArray128& bitArrayExclude) const;
	U32F GetIsBusyForEach(BitArray128& bitArrayOut) const;
	U64 CollectHitReactionStateFlags() const;

	void PostRootLocatorUpdate();
	void OnHitReactionPlayed();

	void ConfigureCharacter(Demeanor demeanor,
							const DC::NpcDemeanorDef* pDemeanorDef,
							const NdAiAnimationConfig* pAnimConfig);

	void EnterNewParentSpace(const Transform& matOldToNew, const Locator& oldParentSpace, const Locator& newParentSpace);

	void SetController(U32F index, AnimActionController* pController);
	virtual const ActionPackController* GetControllerForActionPackType(ActionPack::Type apType) const = 0;
	virtual ActionPackController* GetControllerForActionPackType(ActionPack::Type apType) = 0;
	const ActionPackController* GetControllerForActionPack(const ActionPack* pAp) const;
	ActionPackController* GetControllerForActionPack(const ActionPack* pAp);

	void GetNavAnimControllerIndices(BitArray128& bits) const;

	virtual INdAiLocomotionController* GetLocomotionController() = 0;
	virtual const INdAiLocomotionController* GetLocomotionController() const = 0;

	virtual INavAnimController* GetSwimController() = 0;
	virtual const INavAnimController* GetSwimController() const = 0;

#if ENABLE_NAV_LEDGES
	virtual INdAiClimbController* GetClimbController() = 0;
	virtual const INdAiClimbController* GetClimbController() const = 0;
#endif // ENABLE_NAV_LEDGES

	virtual INdAiWeaponController* GetWeaponController() = 0;
	virtual const INdAiWeaponController* GetWeaponController() const = 0;

	virtual INdAiTraversalController* GetTraversalController() = 0;
	virtual const INdAiTraversalController* GetTraversalController() const = 0;

	virtual INdAiHitController*	GetHitController() = 0;
	virtual const INdAiHitController* GetHitController() const = 0;

	virtual INdAiCinematicController* GetCinematicController() = 0;
	virtual const INdAiCinematicController* GetCinematicController() const = 0;

	virtual AiEntryController* GetEntryController() = 0;
	virtual const AiEntryController* GetEntryController() const = 0;

	virtual U32F GetShouldInterruptNavigationForEach(BitArray128& bitArrayOut) const	{ return 0; }
	virtual U32F GetShouldInterruptSkillsForEach(BitArray128& bitArrayOut) const		{ return 0; }

	virtual const char* GetControllerName(U32F typeIndex) const							{ return ""; }

protected:
	AnimActionController** m_controllerList;
	U32	m_numControllers;
};
