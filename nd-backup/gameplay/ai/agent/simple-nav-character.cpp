/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/ai/agent/simple-nav-character.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/util/bitarray128.h"

#include "gamelib/anim/gesture-controller.h"
#include "gamelib/gameplay/ai/controller/animaction-controller.h"
#include "gamelib/gameplay/ai/controller/nd-traversal-controller.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"

/// --------------------------------------------------------------------------------------------------------------- ///
PROCESS_REGISTER_ABSTRACT(SimpleNavCharacter, NdGameObject);
FROM_PROCESS_DEFINE(SimpleNavCharacter);

/// --------------------------------------------------------------------------------------------------------------- ///
SimpleNavCharacter::SimpleNavCharacter()
	: m_navControl(), m_requestedMotionType(kMotionTypeWalk), m_pAnimationControllers(nullptr), m_pScriptLogger(nullptr)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
SimpleNavCharacter::~SimpleNavCharacter()
{
	if (m_pAnimationControllers)
	{
		m_pAnimationControllers->Destroy();
		m_pAnimationControllers = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err SimpleNavCharacter::Init(const ProcessSpawnInfo& info)
{
	// Extract parameters from the spawn info
	//const SpawnInfo& spawn = static_cast<const SpawnInfo&>(info);

	m_pAnimationControllers = NDI_NEW SimpleAnimationControllers(SimpleAnimationControllers::kNumSimpleControllers);
	if (!m_pAnimationControllers)
		return Err::kErrOutOfMemory;

	Err result = ParentClass::Init(info);
	if (result.Failed())
		return result;

	if (info.GetData<bool>(SID("enable-script-log"), false))
	{
		EnableScriptLog();
	}

	m_navControl.ConfigureNavRadii(0.5f, -1.0f, 1.0f);

	m_emotionControl.SetOwner(this);

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavCharacter::EnableScriptLog()
{
	STRIP_IN_FINAL_BUILD;
	if (!m_pScriptLogger)
	{
		m_pScriptLogger = NDI_NEW(kAllocDebug) AiScriptLogger();
		m_pScriptLogger->Init();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* SimpleNavCharacter::GetOverlayBaseName() const
{
	return "simple-npc";
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavCharacter::SetCurNavPoly(const NavPoly* pPoly, bool teleport)
{ 
	m_navControl.SimpleUpdatePs(GetTranslationPs(), pPoly, this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavCharacter::ResetNavMesh()
{
	if (SimpleNavControl* pNavCon = GetNavControl())
	{
		const bool isStopped = !IsMoving();
		pNavCon->ResetNavMesh(nullptr);
		pNavCon->SimpleUpdatePs(GetTranslationPs(), nullptr, this);

		//if (const NavMesh* pMesh = pNavCon->GetNavMesh())
		//{
		//	Log(this, kNpcLogChannelNav, " - ResetNavMesh connected to nav mesh %s\n", pMesh->GetName());
		//}
		//else
		//{
		//	Log(this, kNpcLogChannelNav, " - ResetNavMesh no nav mesh found\n");
		//}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavCharacter::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	DeepRelocatePointer(m_pAnimationControllers, deltaPos, lowerBound, upperBound);
	DeepRelocatePointer(m_pScriptLogger, deltaPos, lowerBound, upperBound);

	ParentClass::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavCharacter::OnKillProcess()
{
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		m_navControl.ResetNavMesh(nullptr, this);
	}

	if (m_pScriptLogger)
	{
		m_pScriptLogger->FreeMemory();
	}

	ParentClass::OnKillProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionType SimpleNavCharacter::GetCurrentMotionType() const 
{
	//const INdAiLocomotionController* pLocomotionController = GetNdAnimationControllers()->GetLocomotionController();
	//AI_ASSERT(pLocomotionController);
	//return pLocomotionController->GetCurrentMotionType();
	return m_requestedMotionType; // @SNPCTAP FOR NOW
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 SimpleNavCharacter::GetCurrentMtSubcategory() const 
{
	//const INdAiLocomotionController* pLocomotionController = GetNdAnimationControllers()->GetLocomotionController();
	//AI_ASSERT(pLocomotionController);
	//return pLocomotionController->GetCurrentMtSubcategory();
	return GetRequestedMtSubcategory(); // @SNPCTAP FOR NOW
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point SimpleNavCharacter::GetFacePositionPs() const
{
	return (GetLocatorPs().GetPosition() + GetLocalZ(GetRotationPs()) * 100.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SimpleNavCharacter::IsBusy(BusyExcludeFlags excludeFlags /*= kExcludeNone*/) const
{
	BitArray128 excludeControllersFlags;
	excludeControllersFlags.Clear();
	return IsBusyExcludingControllers(excludeControllersFlags, excludeFlags);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SimpleNavCharacter::IsBusyExcludingControllers(BitArray128& excludeControllerFlags,
													BusyExcludeFlags excludeFlags /* = kExcludeNone */) const
{
	const bool excludeFades = !(excludeFlags & kExcludeFades);

	BitArray128 isBusyFlags;
	GetAnimationControllers()->GetIsBusyForEach(isBusyFlags);

	BitArray128 isNonExcludedBusyFlags;
	isNonExcludedBusyFlags.m_value[0] = isBusyFlags.m_value[0] & ~excludeControllerFlags.m_value[0];
	isNonExcludedBusyFlags.m_value[1] = isBusyFlags.m_value[1] & ~excludeControllerFlags.m_value[1];

	bool bAnimationControllerBusy = !isNonExcludedBusyFlags.Empty();

	// removing vox check to fix issues with NPCs waiting before getting to their zone
	bool bVoxControllerBusy = false; //GetVoxControllerConst().IsBusy();
	bool bFadesInProgress = excludeFades && (GetAnimControl()->GetNumFadesInProgress() > 0);

	bool spawning = false; //IsSpawnAnimPlaying();
//	if (!spawning)
//	{
//		if (const AnimStateLayer* pBaseLayer = GetAnimControl()->GetBaseStateLayer())
//		{
//			pBaseLayer->WalkInstancesNewToOld(WalkCheckSpawning, &spawning);
//		}
//	}

	bool retval = bAnimationControllerBusy || bVoxControllerBusy || (bFadesInProgress && !spawning);
	U32 retvalAsU32 = retval;

	return retval;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame SimpleNavCharacter::AdjustBoundFrameToUpright(const BoundFrame& bf) const
{
	const BoundFrame& myBf = GetBoundFrame();

	BoundFrame ret = bf;

	//switch (m_motionConfig.m_adjustUprightMode)
	//{
	//case MotionConfig::kAdjustUprightInWorldSpace:
		{
			// Normalize the and force the rotation to be upright.
			Quat newRotationWs = bf.GetRotationWs();
			newRotationWs.SetX(0.0f);
			newRotationWs.SetZ(0.0f);
			newRotationWs = Normalize(newRotationWs);
			ret.SetRotationWs(newRotationWs);
		}
	//	break;
	//
	//case MotionConfig::kAdjustUprightInParentSpace:
	//	if (bf.IsSameBinding(myBf.GetBinding()))
	//	{
	//		// Normalize the and force the rotation to be upright.
	//		Quat newRotationPs = bf.GetRotationPs();
	//		newRotationPs.SetX(0.0f);
	//		newRotationPs.SetZ(0.0f);
	//		newRotationPs = Normalize(newRotationPs);
	//		ret.SetRotationPs(newRotationPs);
	//	}
	//	else
	//	{
	//		Quat newRotationInMyPs = myBf.GetParentSpace().UntransformLocator(bf.GetLocatorWs()).GetRotation();
	//		newRotationInMyPs.SetX(0.0f);
	//		newRotationInMyPs.SetZ(0.0f);
	//		newRotationInMyPs = Normalize(newRotationInMyPs);
	//		ret.SetRotationPs(newRotationInMyPs);
	//	}
	//	break;
	//
	//default:
	//	ASSERT(false);
	//	break;
	//};

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point SimpleNavCharacter::GetAimOriginPs() const
{
	const Locator locWs = GetSite(SID("AimAtPos"));
	const Locator& parentSpace = GetParentSpace();
	return parentSpace.UntransformLocator(locWs).Pos();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point SimpleNavCharacter::GetLookOriginPs() const
{
	const Locator locWs = GetSite(SID("LookAtPos"));
	const Locator& parentSpace = GetParentSpace();
	return parentSpace.UntransformLocator(locWs).Pos();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleNavCharacter::DebugDraw(ScreenSpaceTextPrinter* pPrinter) const
{
	bool isSelected = DebugSelection::Get().IsProcessSelected(this);

	if (m_pScriptLogger && isSelected && g_navCharOptions.m_npcScriptLogToMsgCon)
	{
		DoutBase* pMsgCon = GetMsgOutput(kMsgConNonpauseable);
		pMsgCon->Printf("SCRIPT LOG ------ %s ------------ pid-%d\n", GetName(), U32(GetProcessId()));
		m_pScriptLogger->Dump(pMsgCon);
	}

	if (m_pScriptLogger && (isSelected || DebugSelection::Get().GetCount() == 0))
	{
		if (g_navCharOptions.m_npcScriptLogOverhead)
		{
			m_pScriptLogger->DebugDraw(pPrinter, GetClock());
		}
	}

	if (const IGestureController* pGestureController = GetGestureController())
	{
		pGestureController->DebugDraw();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
SimpleAnimationControllers::~SimpleAnimationControllers() {}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPackController* SimpleAnimationControllers::GetControllerForActionPackType(ActionPack::Type apType)
{
	if (apType == ActionPack::kTraversalActionPack)
		return GetTraversalController();
	else if (apType == ActionPack::kCinematicActionPack)
		return PunPtr<ActionPackController*>(m_controllerList[kSimpleCinematicController]);
	else
		return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleAnimationControllers::PostRootLocatorUpdate()
{
	for (U32F i = 0; i < m_numControllers; ++i)
	{
		if (m_controllerList[i])
		{
			m_controllerList[i]->PostRootLocatorUpdate();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ActionPackResolveInput MakeDefaultResolveInput(const SimpleNavCharacter* pNavChar)
{
	ActionPackResolveInput input;
	input.m_frame = pNavChar->GetBoundFrame();

	input.m_moving		  = pNavChar->IsMoving();
	input.m_motionType	  = pNavChar->GetCurrentMotionType();
	input.m_mtSubcategory = pNavChar->GetRequestedMtSubcategory();
	input.m_velocityPs	  = pNavChar->GetVelocityPs();

	if (input.m_motionType == kMotionTypeMax)
	{
		input.m_motionType = pNavChar->GetRequestedMotionType();
	}

	if (!input.m_moving)
	{
		const bool isMoving = pNavChar->IsMoving(); // @SNPCTAP real NPCs also check for state 'Starting here
		if (isMoving)
		{
			input.m_moving = true;
			input.m_motionType = pNavChar->GetRequestedMotionType();
		}
	}

	return input;
}
