/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/anim/anim-defines.h"

#include "gamelib/gameplay/ai/component/ai-script-logger.h"
#include "gamelib/gameplay/ai/controller/nd-animation-controller-config.h"
#include "gamelib/gameplay/ai/controller/nd-animation-controllers.h"
#include "gamelib/gameplay/ai/nav-character-anim-defines.h"
#include "gamelib/gameplay/nav/action-pack-entry-def.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/scriptx/h/nav-character-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ActionPackController;
class AnimActionController;
class INavAnimController;
class INdAiCinematicController;
class INdAiClimbController;
class INdAiLocomotionController;
class INdAiTraversalController;
class INdAiWeaponController;
class NavMesh;
class NavPoly;
class ProcessSpawnInfo;
class SimpleFaceController;
class TraversalActionPack;
struct BitArray128;
struct INdAiHitController;
struct NavAnimHandoffDesc;

/// --------------------------------------------------------------------------------------------------------------- ///
// SimpleAnimationControllers
//
// A simple implementation of NdAnimationControllers for use with SimpleNavCharacter/SimpleNpc.
/// --------------------------------------------------------------------------------------------------------------- ///
class SimpleAnimationControllers : public NdAnimationControllers
{
public:
	enum ControllerTypes
	{
		kSimpleTraversalController,
		kSimpleCinematicController,
		kSimpleFaceController,
		kNumSimpleControllers
	};

	SimpleAnimationControllers(U32 numControllers) : NdAnimationControllers(numControllers) {}
	virtual ~SimpleAnimationControllers() override;

	virtual ActionPackController* GetControllerForActionPackType(ActionPack::Type apType) override;
	virtual const ActionPackController* GetControllerForActionPackType(ActionPack::Type apType) const override
	{
		return const_cast<SimpleAnimationControllers*>(this)->GetControllerForActionPackType(apType);
	}

	virtual INdAiLocomotionController* GetLocomotionController() override { return nullptr; }
	virtual const INdAiLocomotionController* GetLocomotionController() const override { return nullptr; }

	virtual INavAnimController* GetSwimController() override { return nullptr; }
	virtual const INavAnimController* GetSwimController() const override { return nullptr; }

#if ENABLE_NAV_LEDGES
	virtual INdAiClimbController* GetClimbController() override { return nullptr; }
	virtual const INdAiClimbController* GetClimbController() const override { return nullptr; }
#endif // ENABLE_NAV_LEDGES

	virtual INdAiWeaponController* GetWeaponController() override { return nullptr; }
	virtual const INdAiWeaponController* GetWeaponController() const override { return nullptr; }

	virtual INdAiTraversalController* GetTraversalController() override
	{
		return PunPtr<INdAiTraversalController*>(m_controllerList[kSimpleTraversalController]);
	}
	virtual const INdAiTraversalController* GetTraversalController() const override
	{
		return PunPtr<const INdAiTraversalController*>(m_controllerList[kSimpleTraversalController]);
	}

	virtual INdAiHitController* GetHitController() override { return nullptr; }
	virtual const INdAiHitController* GetHitController() const override { return nullptr; }

	virtual INdAiCinematicController* GetCinematicController() override
	{
		return PunPtr<INdAiCinematicController*>(m_controllerList[kSimpleCinematicController]);
	}
	virtual const INdAiCinematicController* GetCinematicController() const override
	{
		return PunPtr<const INdAiCinematicController*>(m_controllerList[kSimpleCinematicController]);
	}

	virtual AiEntryController* GetEntryController() override { return nullptr; }
	virtual const AiEntryController* GetEntryController() const override { return nullptr; }

	void PostRootLocatorUpdate();
};

/// --------------------------------------------------------------------------------------------------------------- ///
// SimpleNavCharacter
//
// A stripped-down version of NavCharacter; base class of SimpleNpc.
/// --------------------------------------------------------------------------------------------------------------- ///
class SimpleNavCharacter : public NdGameObject
{
	typedef NdGameObject ParentClass;

public:
	FROM_PROCESS_DECLARE(SimpleNavCharacter);

	SimpleNavCharacter();
	virtual ~SimpleNavCharacter() override;

	virtual Err Init(const ProcessSpawnInfo& info) override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void OnKillProcess() override;
	virtual int GetChildScriptEventCapacity() const override { return 16; }

	virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const;
	virtual void DebugDraw(const DebugDrawFilter& filter) const override { ParentClass::DebugDraw(filter); }

	void EnableScriptLog();

	DoutMemChannels* GetScriptDebugLog() const { return m_pScriptLogger ? m_pScriptLogger->GetScriptDebugLog() : nullptr; }
	AiScriptLogger* GetScriptLogger() const { return m_pScriptLogger; }

	// NavPoly Tracking
	float GetCurrentNavAdjustRadius() const { return m_navControl.GetCurrentNavAdjustRadius(); }
	float GetMaximumNavAdjustRadius() const { return m_navControl.GetMaximumNavAdjustRadius(); }
	float GetMovingNavAdjustRadius() const { return m_navControl.GetMovingNavAdjustRadius(); }
	float GetIdleNavAdjustRadius() const { return m_navControl.GetIdleNavAdjustRadius(); }

	SimpleNavControl* GetNavControl() { return &m_navControl; }
	const SimpleNavControl* GetNavControl() const { return const_cast<SimpleNavCharacter*>(this)->GetNavControl(); }
	void SetCurNavPoly(const NavPoly* pPoly, bool teleport);
	const NavPoly*	GetCurNavPoly()	const	{ return m_navControl.GetNavLocation().ToNavPoly(); }
	const NavMesh*	GetCurNavMesh() const	{ return m_navControl.GetNavLocation().ToNavMesh(); }
	void ResetNavMesh();

	const Point GetFacePositionPs() const;

	virtual const char* GetOverlayBaseName() const override;

	// change how the character moves (walk, run, sprint, etc) without changing the path
	MotionType GetCurrentMotionType() const;
	StringId64 GetCurrentMtSubcategory() const;
	virtual StringId64 GetRequestedMtSubcategory() const = 0;
	MotionType GetRequestedMotionType() const							{ return m_requestedMotionType; }
	bool IsStrafing() const												{ return false; }

	I32 GetRequestedDcDemeanor() const									{ return 0; } // @SNPCTAP zero is ambient which should be fine FOR NOW anyway
	Demeanor GetRequestedDemeanor() const								{ return Demeanor(0); } // @SNPCTAP zero is ambient which should be fine FOR NOW anyway
	Demeanor GetCurrentDemeanor() const									{ return Demeanor(0); } // @SNPCTAP zero is ambient which should be fine FOR NOW anyway
	Demeanor GetCinematicActionPackDemeanor() const						{ return Demeanor(0); } // @SNPCTAP zero is ambient which should be fine FOR NOW anyway
	virtual StringId64 SidFromDemeanor(const Demeanor& demeanor) const	{ return INVALID_STRING_ID_64; }
	GunState GetCurrentGunState() const									{ return kGunStateHolstered; }
	bool HasDemeanor(I32F demeanorIntval) const							{ return false; }

			SimpleAnimationControllers*	GetAnimationControllers()		{ return m_pAnimationControllers; }
	const	SimpleAnimationControllers*	GetAnimationControllers() const	{ return m_pAnimationControllers; }

	bool IsBusy(BusyExcludeFlags excludeFlags = kExcludeNone) const;
	bool IsBusyExcludingControllers(BitArray128& excludeControllerFlags,
									BusyExcludeFlags excludeFlags = kExcludeNone) const;

	virtual bool IsMoving() const = 0;

	const NdAnimControllerConfig* GetAnimControllerConfig() const { return &m_animControllerConfig; }
	NdAnimControllerConfig* GetAnimControllerConfig() { return &m_animControllerConfig; }

	//------------------------------------------------------------------------
	// Action packs
	//------------------------------------------------------------------------

	virtual ActionPack* GetReservedActionPack() const = 0;
	virtual ActionPack* GetEnteredActionPack() const = 0;
	virtual const TraversalActionPack* GetTraversalActionPack() const = 0;

	virtual void ConfigureNavigationHandOff(const NavAnimHandoffDesc& desc,
											const char* sourceFile,
											U32F sourceLine,
											const char* sourceFunc)
	{
	}

	BoundFrame AdjustBoundFrameToUpright(const BoundFrame& bf) const;

	virtual Point GetLookAtPositionPs() const = 0;
	virtual bool WantNaturalLookAt() const = 0;

	virtual StringId64 GetCapCharacterId() const = 0;

	virtual EmotionControl* GetEmotionControl() override { return &m_emotionControl; }
	virtual const EmotionControl* GetEmotionControl() const override { return &m_emotionControl; }

	virtual Point GetAimOriginPs() const override;
	virtual Point GetLookOriginPs() const override;

protected:
	SimpleNavControl			m_navControl;
	EmotionControl				m_emotionControl;

	float						m_goalRadius;
	MotionType					m_requestedMotionType;
	SimpleAnimationControllers*	m_pAnimationControllers;
	NdAnimControllerConfig		m_animControllerConfig;

	AiScriptLogger*				m_pScriptLogger;

	PROCESS_IS_RAW_TYPE_DEFINE(SimpleNavCharacter);
};

/// --------------------------------------------------------------------------------------------------------------- ///
FWD_DECL_PROCESS_HANDLE(SimpleNavCharacter);

PROCESS_DECLARE(SimpleNavCharacter);

const ActionPackResolveInput MakeDefaultResolveInput(const SimpleNavCharacter* pNavChar);
