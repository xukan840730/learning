/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/nd-gesture-util.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-util.h"
#include "ndlib/frame-params.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/process-mgr.h"

#include "gamelib/anim/gesture-node.h"
#include "gamelib/anim/gesture-target.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/nd-locatable.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/script/fact-map.h"
#include "gamelib/scriptx/h/nd-gesture-defines.h"
#include "gamelib/scriptx/h/nd-gesture-script-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
#define MERGE_PARAM(ParamName)                                                                                         \
	do                                                                                                                 \
	{                                                                                                                  \
		if (overrideParams.ParamName##Valid)                                                                           \
		{                                                                                                              \
			params.ParamName##Valid = true;                                                                            \
			params.ParamName		= overrideParams.ParamName;                                                        \
		}                                                                                                              \
	} while (false)

/// --------------------------------------------------------------------------------------------------------------- ///
static Gesture::Config* s_pConfig = nullptr;

/// --------------------------------------------------------------------------------------------------------------- ///
namespace Gesture
{
	const PlayArgs g_defaultPlayArgs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::Config* Gesture::Config::Get()
{
	ANIM_ASSERTF(s_pConfig, ("Gesture config left unset by the game"));
	return s_pConfig;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Gesture::Config::Set(Config* pConfig)
{
	s_pConfig = pConfig;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::Err Gesture::PlayArgs::EvalParamsTarget(TargetBuffer* pOutTarget) const
{
	if (m_dcParams.m_toPointValid)
	{
		NDI_NEW (pOutTarget) Gesture::TargetPoint(*m_dcParams.m_toPoint);

		return Err();
	}

	if (m_dcParams.m_toValid)
	{
		ProcessMgr* pProcessMgr = EngineComponents::GetProcessMgr();
		const LevelMgr* pLevelMgr = EngineComponents::GetLevelMgr();
		const Process* pTargetProcess = pProcessMgr ? pProcessMgr->LookupProcessByUserId(m_dcParams.m_to) : nullptr;
		
		if (const NdLocatableObject* pToObject = NdLocatableObject::FromProcess(pTargetProcess))
		{
			if (m_dcParams.m_toJointValid)
			{
				if (const NdGameObject* pToGameObject = NdGameObject::FromProcess(pToObject))
				{
					const I32 jointIndex = pToGameObject->FindJointIndex(m_dcParams.m_toJoint);

					if (jointIndex >= 0)
					{
						NDI_NEW (pOutTarget) Gesture::TargetObjectJoint(pToGameObject, jointIndex);

						return Err();
					}
					else
					{
						return Err(SID("joint-not-found"));
					}
				}
				else
				{
					return Err(SID("joint-target-only-valid-with-game-objects"));
				}
			}
			else
			{
				NDI_NEW (pOutTarget) Gesture::TargetObject(*pToObject);

				return Err();
			}
		}
		else if (const EntitySpawner* pSpawner = pLevelMgr->LookupEntitySpawnerByBareNameId(m_dcParams.m_to))
		{
			NDI_NEW (pOutTarget) Gesture::TargetPoint(pSpawner->GetBoundFrame().GetTranslationWs());

			return Err();
		}
		else
		{
			return Err(SID("gesture-target-invalid"));
		}
	}

	if (m_targetSupplied)
	{
		*pOutTarget = m_target;
		return Err();
	}

	return Err(SID("no-target-supplied"));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Gesture::MergeParams(DC::GesturePlayParams& params, const DC::GesturePlayParams& overrideParams)
{
	MERGE_PARAM(m_loop);
	MERGE_PARAM(m_to);
	MERGE_PARAM(m_toPoint);
	MERGE_PARAM(m_wait);
	MERGE_PARAM(m_priority);
	MERGE_PARAM(m_legFixIk);
	MERGE_PARAM(m_handFixIk);
	MERGE_PARAM(m_toJoint);
	MERGE_PARAM(m_startPhase);
	MERGE_PARAM(m_maxBlendIn);
	MERGE_PARAM(m_springConstant);
	MERGE_PARAM(m_springConstantReduced);
	MERGE_PARAM(m_flip);
	MERGE_PARAM(m_playbackRate);
	MERGE_PARAM(m_layerIndex);
	MERGE_PARAM(m_propName);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static DC::GesturePlayParams s_defaultGestureParams;
static bool s_defaultGestureParamsValid = false;

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::GesturePlayParams& Gesture::GetDefaultParams()
{
	if (!s_defaultGestureParamsValid)
	{
		s_defaultGestureParams.m_loopValid = true;
		s_defaultGestureParams.m_loop	   = false;

		s_defaultGestureParams.m_waitValid = true;
		s_defaultGestureParams.m_wait	   = false;

		s_defaultGestureParams.m_priorityValid = true;
		s_defaultGestureParams.m_priority	   = Config::Get()->GetDefaultGesturePriority();

		s_defaultGestureParams.m_legFixIkValid = true;
		s_defaultGestureParams.m_legFixIk	   = true;

		s_defaultGestureParams.m_springConstantValid = true;
		s_defaultGestureParams.m_springConstant		 = Gesture::kDefaultGestureSpringConstant;
		s_defaultGestureParams.m_springDampingRatio	 = Gesture::kDefaultDampingRatio;

		s_defaultGestureParamsValid = true;
	}

	return s_defaultGestureParams;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::Err Gesture::ValidateParams(const DC::GesturePlayParams& params)
{
	if (params.m_toPointValid && params.m_toPoint == nullptr)
	{
		return Err(SID(":to-point parameter was nullptr"));
	}

	return Err();
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 Gesture::GetInputGestureId(const DC::AnimNodeGesture* pDcGestureNode,
									  const SnapshotAnimNodeTreeParams& params)
{
	if (!pDcGestureNode)
		return INVALID_STRING_ID_64;

	StringId64 nameId = pDcGestureNode->m_gestureName;

	if (pDcGestureNode->m_gestureNameFunc)
	{
		ScriptValue argv[2];
		argv[0].m_stringId = nameId;
		argv[1].m_pointer = params.m_pInfoCollection;

		const ScriptValue scriptVal = ScriptManager::Eval(pDcGestureNode->m_gestureNameFunc,
														  ARRAY_ELEMENT_COUNT(argv),
														  argv);

		nameId = scriptVal.m_stringId;
	}

	return nameId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::GestureDef* Gesture::LookupGesture(StringId64 const gestureId)
{
	return ScriptManager::LookupInNamespace<DC::GestureDef>(gestureId, SID("gestures"), nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 Gesture::GetPhaseAnim(const DC::GestureAnims& gesture)
{
	StringId64 phaseAnim = gesture.m_animPairs[0].m_partialAnim;

	if ((phaseAnim == INVALID_STRING_ID_64) || (phaseAnim == SID("null-add")))
	{
		phaseAnim = gesture.m_animPairs[0].m_additiveAnim;
	}

	return phaseAnim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::CachedGestureRemap Gesture::RemapGesture(const CachedGestureRemap& prevRemap, const AnimControl* pAnimControl)
{
	if (prevRemap.GetSourceId() == INVALID_STRING_ID_64)
	{
		return CachedGestureRemap();
	}

	CachedGestureRemap result;

	result.m_animLookup = pAnimControl->LookupAnimCached(prevRemap.m_animLookup);

	StringId64 const animLookupId = result.m_animLookup.GetFinalResolvedId();
	result.m_finalGestureId = (animLookupId == SID("null")) ? INVALID_STRING_ID_64 : animLookupId;

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 Gesture::RemapGesture(const StringId64 gestureId, AnimControl* pAnimControl)
{
	if (gestureId == INVALID_STRING_ID_64)
	{
		return INVALID_STRING_ID_64;
	}

	AnimOverlaySnapshot* pOverlaySnapshot = pAnimControl ? pAnimControl->GetAnimOverlaySnapshot() : nullptr;
	const StringId64 remappedGestureId = pOverlaySnapshot->LookupTransformedAnimId(gestureId);

	if (remappedGestureId == SID("null"))
	{
		return INVALID_STRING_ID_64;
	}

	return remappedGestureId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 Gesture::RemapGestureAndIncrementVariantIndices(const StringId64 gestureId, AnimControl* pAnimControl)
{
	if (gestureId == INVALID_STRING_ID_64)
	{
		return INVALID_STRING_ID_64;
	}

	AnimOverlaySnapshot* pOverlaySnapshot = pAnimControl ? pAnimControl->GetAnimOverlaySnapshot() : nullptr;
	const StringId64 remappedGestureId = pOverlaySnapshot->LookupTransformedAnimIdAndIncrementVariantIndices(gestureId);

	if (remappedGestureId == SID("null"))
	{
		return INVALID_STRING_ID_64;
	}

	return remappedGestureId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
LimbLockBits Gesture::GetLimbLockBitsForGesture(const DC::GestureAnims* pGesture, const NdGameObject* pObject)
{
	const AnimControl* pAnimControl = pObject ? pObject->GetAnimControl() : nullptr;
	if (!pAnimControl)
	{
		return LimbLockBits(0x0);
	}

	LimbLockBits bits = static_cast<LimbLockBits>(0x0);

	for (U32 i = 0; i < pGesture->m_numAnimPairs; ++i)
	{
		const DC::GestureAnimPair& animPair = pGesture->m_animPairs[i];

		for (U32 j = 0; j < 2; ++j)
		{
			const StringId64 anim = (j == 0) ? animPair.m_partialAnim : animPair.m_additiveAnim;

			if (anim != INVALID_STRING_ID_64)
			{
				const ArtItemAnim* pAnim = pAnimControl->LookupAnimNoOverlays(anim).ToArtItem();
				if (pAnim)
				{
					bits = static_cast<LimbLockBits>(bits | GetLimbLockBitsForAnim(pAnim, pObject));
				}
			}
		}
	}

	return bits;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SphericalCoords Gesture::ApRefToCoords(Quat_arg apRefRot, const bool flipped)
{
	const Vector animDirZ = GetLocalZ(apRefRot);
	const Vector animDirY = GetLocalY(apRefRot);

	const float thetaRadZ = -Atan2(animDirZ.X(), animDirZ.Z());
	const float phiRad = SafeAsin(animDirZ.Y());

	const float thetaRadY = -Atan2(animDirY.X(), animDirY.Z());

	const float thetaRad = LerpScale(80.0f, 85.0f, thetaRadZ, thetaRadY, RADIANS_TO_DEGREES(Abs(phiRad)));

	SphericalCoords ret = SphericalCoords::FromThetaPhiRad(thetaRad, phiRad);

	if (flipped)
	{
		ret.Theta() *= -1.0f;
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::AlternativeIndex Gesture::DesiredAlternative(const DC::GestureDef* pGesture,
													  const FactDictionary* pFacts,
													  const DC::GestureAlternative*& pOutDesiredAlternative,
													  bool debug)
{
	pOutDesiredAlternative = nullptr;

	if (!pFacts)
	{
		return kAlternativeIndexNone;
	}

	const DC::FactMap* pAlternatives = pGesture->m_gestureAlternatives;
	if (!pAlternatives)
	{
		return kAlternativeIndexNone;
	}

	FactMap::IDebugPrint* pDebugger = nullptr;

	if (FALSE_IN_FINAL_BUILD(debug))
	{
		pDebugger = NDI_NEW (kAllocSingleFrame) FactMap::DebugPrint<DC::GestureAlternative>(kMsgConPauseable);
		MsgConPauseable("Gesture Alternatives for %s:\n", DevKitOnly_StringIdToString(pGesture->m_name));
	}

	const DC::GestureAlternative* pChosenAlternative = FactMap::Lookup<DC::GestureAlternative>(pAlternatives,
																							   pFacts,
																							   pDebugger);
	if (!pChosenAlternative)
	{
		return kAlternativeIndexNone;
	}

	for (I32 i = 0; i < pAlternatives->m_numEntries; ++i)
	{
		if (pChosenAlternative == pAlternatives->m_values[i].m_ptr)
		{
			pOutDesiredAlternative = pChosenAlternative;
			return i;
		}
	}

	// In this case, the desired alternative is simply the original gesture.
	return kAlternativeIndexNone;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Gesture::AlternativeIndex Gesture::DesiredNoiseAlternative(const DC::GestureDef* pGesture,
														   const FactDictionary* pFacts,
														   bool debug)
{
	if (!pFacts)
	{
		return kAlternativeIndexNone;
	}

	const DC::FactMap* pAlternatives = pGesture->m_offsetNoiseAlternatives;
	if (!pAlternatives)
	{
		return kAlternativeIndexNone;
	}

	FactMap::IDebugPrint* pDebugger = nullptr;

	if (FALSE_IN_FINAL_BUILD(debug))
	{
		pDebugger = NDI_NEW(kAllocSingleFrame) FactMap::DebugPrint<DC::GestureNoiseDef>(kMsgConPauseable);
		MsgConPauseable("Gesture Noise Alternatives for %s:\n", DevKitOnly_StringIdToString(pGesture->m_name));
	}

	const void* pChosenAlternative = FactMap::LookupPtr(pAlternatives, pFacts, pDebugger);

	if (!pChosenAlternative)
	{
		return kAlternativeIndexNone;
	}

	for (I32 i = 0; i < pAlternatives->m_numEntries; ++i)
	{
		if (pChosenAlternative == pAlternatives->m_values[i].m_ptr)
		{
			return i;
		}
	}

	// In this case, the desired alternative is simply the original gesture.
	return kAlternativeIndexNone;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::GestureAnims* Gesture::GetGestureAnims(const DC::GestureDef* pGesture,
												 AlternativeIndex iAlt /* = kAlternativeIndexUnspecified */)
{
	if (!pGesture || (iAlt < 0))
	{
		return pGesture;
	}
	
	const DC::FactMap* pAlternatives = pGesture->m_gestureAlternatives;
	if (!pAlternatives || iAlt >= pAlternatives->m_numEntries)
	{
		return pGesture;
	}

	return (const DC::GestureAnims*)pAlternatives->m_values[iAlt].m_ptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::GestureAlternative* Gesture::GetGestureAlternative(const DC::GestureDef* pGesture, AlternativeIndex iAlt)
{
	if (iAlt < 0)
	{
		return nullptr;
	}

	const DC::FactMap* pAlternatives = pGesture->m_gestureAlternatives;
	if (!pAlternatives || iAlt >= pAlternatives->m_numEntries)
	{
		return nullptr;
	}

	return (const DC::GestureAlternative*)pAlternatives->m_values[iAlt].m_ptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 Gesture::GetAlternativeName(const DC::GestureDef* pGesture, AlternativeIndex iAlt)
{
	StringId64 altName = INVALID_STRING_ID_64;

	if (const DC::GestureAlternative* pAlt = GetGestureAlternative(pGesture, iAlt))
	{
		altName = pAlt->m_altName;

		if (altName == INVALID_STRING_ID_64 && iAlt >= 0)
		{
			altName = StringId64ConcatInteger(SID("gesture-alt-"), iAlt);
		}
	}

	return altName;
}


/// --------------------------------------------------------------------------------------------------------------- ///
struct FindSpecificGestureNodeParams
{
	StringId64 m_gestureId = INVALID_STRING_ID_64;
	const IGestureNode* m_pFoundNode = nullptr;
	float m_phase = -1.0f;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool FindSpecificGestureNode(const AnimStateSnapshot* pSnapshot,
									const AnimSnapshotNode* pNode,
									const AnimSnapshotNodeBlend* pParentBlendNode,
									float combinedBlend,
									uintptr_t userData)
{
	const IGestureNode* pGestureNode = IGestureNode::FromAnimNode(pNode);
	ANIM_ASSERT(pGestureNode);
	if (!pGestureNode)
		return true;

	FindSpecificGestureNodeParams* pParams = (FindSpecificGestureNodeParams*)userData;

	if ((pGestureNode->GetResolvedGestureNameId() == pParams->m_gestureId)
		|| (INVALID_STRING_ID_64 == pParams->m_gestureId))
	{
		if (pGestureNode->IsUsingDetachedPhase())
		{
			pParams->m_phase = pSnapshot->m_statePhase;
		}
		else
		{
			pParams->m_phase = pGestureNode->GetDetachedPhase();
		}

		pParams->m_pFoundNode = pGestureNode;
		
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const IGestureNode* Gesture::FindPlayingGesture(const AnimStateLayer* pStateLayer, StringId64 gestureId)
{
	const AnimStateInstance* pCurrentState = pStateLayer ? pStateLayer->CurrentStateInstance() : nullptr;

	if (!pCurrentState)
		return nullptr;

	FindSpecificGestureNodeParams params;
	params.m_gestureId = gestureId;

	const AnimStateSnapshot& stateSnapshot = pCurrentState->GetAnimStateSnapshot();
	stateSnapshot.VisitNodesOfKind(SID("IGestureNode"), FindSpecificGestureNode, (uintptr_t)&params);

	return params.m_pFoundNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float Gesture::GetGesturePhase(const AnimStateLayer* pStateLayer, StringId64 gestureId)
{
	const AnimStateInstance* pCurrentState = pStateLayer ? pStateLayer->CurrentStateInstance() : nullptr;

	if (!pCurrentState)
		return -1.0f;

	FindSpecificGestureNodeParams params;
	params.m_gestureId = gestureId;

	const AnimStateSnapshot& stateSnapshot = pCurrentState->GetAnimStateSnapshot();
	stateSnapshot.VisitNodesOfKind(SID("IGestureNode"), FindSpecificGestureNode, (uintptr_t)&params);

	if (params.m_pFoundNode)
	{
		return params.m_phase;
	}

	return -1.0f;
}

