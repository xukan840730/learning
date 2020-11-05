/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-state.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/anim-transition-search.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/scriptx/h/dc-types.h"
#include "ndlib/scriptx/h/range-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
STATIC_ASSERT(sizeof(DC::AnimTransitionConditionLambda) > sizeof(DC::AnimTransitionCondition));

/// --------------------------------------------------------------------------------------------------------------- ///
bool CanPathfindThrough(const DC::AnimTransition* trans, StringId64 destState)
{
	if (trans->m_flags & DC::kAnimTransitionFlagNoPathfindThrough)
		return false;

	if (trans->m_flags & DC::kAnimTransitionFlagPathfindDestOnly && trans)
	{
		if (destState != trans->m_state->m_name.m_symbol)
		{
			return false;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool RangeActive(const DC::Range& range, float val)
{
	return (!(range.m_flags & DC::kRangeFlagUseUpper) || val <= range.m_upper) &&
		(!(range.m_flags & DC::kRangeFlagUseLower) || val >= range.m_lower);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChannelJointBlend(ndanim::JointParams* pDest, const ndanim::JointParams& start, const ndanim::JointParams& end, float tt)
{
	const Scalar scalarTT(tt);
	pDest->m_quat = Slerp(start.m_quat, end.m_quat, scalarTT);
	pDest->m_trans = Lerp(start.m_trans, end.m_trans, scalarTT);
	pDest->m_scale = Lerp(start.m_scale, end.m_scale, scalarTT);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimChannelJointBlendRadial(ndanim::JointParams* pDest, const ndanim::JointParams& start, const ndanim::JointParams& end, float tt)
{
	const Scalar scalarTT(tt);
	pDest->m_quat = Slerp(start.m_quat, end.m_quat, scalarTT);
	Vector startVec(start.m_trans - kOrigin);
	Vector endVec(end.m_trans - kOrigin);
	Vector finalDir = Slerp(SafeNormalize(startVec,startVec) , SafeNormalize(endVec, endVec), scalarTT);

	finalDir = SafeNormalize(finalDir, kUnitZAxis) * Lerp(Length(startVec), Length(endVec), scalarTT);
	pDest->m_trans = kOrigin + finalDir;
	pDest->m_scale = Lerp(start.m_scale, end.m_scale, scalarTT);
}

/// --------------------------------------------------------------------------------------------------------------- ///
///  AnimTransitionGroup
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
static bool AnimTransitionGroupExists(const DC::AnimTransitionGroup* pGroup, StringId64 transitionId, const TransitionQueryInfo& info)
{
	if (!pGroup)
		return false;

	if ((!pGroup) || (pGroup->m_count == 0) || (!pGroup->m_array))
		return false;

	const U32F numTransitions = pGroup->m_count;
	if (numTransitions)
	{
		const DC::AnimTransition* pTransitions = pGroup->m_array;
		for (U32F i = 0; i < numTransitions; ++i)
		{
			if (pTransitions[i].m_flags & DC::kAnimTransitionFlagGroup)
			{
				const DC::AnimTransitionGroup* pTransitionGroup = pTransitions[i].m_group;
				if (AnimTransitionGroupExists(pTransitionGroup, transitionId, info))
					return true;
			}
			else if (pTransitions[i].m_name == transitionId)
			{
				return true;
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool AnimTransitionGroupValid(const DC::AnimTransitionGroup* pGroup, StringId64 transitionId, const TransitionQueryInfo& info)
{
	if (!pGroup)
		return false;

	if ((!pGroup) || (pGroup->m_count == 0) || (!pGroup->m_array))
		return false;

	const U32F numTransitions = pGroup->m_count;
	if (numTransitions)
	{
		const DC::AnimTransition* pTransitions = pGroup->m_array;
		for (U32F i = 0; i < numTransitions; ++i)
		{
			if (pTransitions[i].m_flags & DC::kAnimTransitionFlagGroup)
			{
				const DC::AnimTransitionGroup* pTransitionGroup = pTransitions[i].m_group;
				if (AnimTransitionGroupValid(pTransitionGroup, transitionId, info))
					return true;
			}
			else if (pTransitions[i].m_name == transitionId)
			{
				if (pTransitions[i].m_flags & DC::kAnimTransitionFlagCheckPhaseAnimExists)
				{
					const DC::AnimState* pState = pTransitions[i].m_state;
					const StringId64 baseAnimId = pState->m_phaseAnimName;
					StringId64 animId = baseAnimId;

					if (pState->m_phaseAnimFunc && info.m_pInfoCollection)
					{
						PROFILE(Animation, ATG_PhaseAnimFunc);

						DC::AnimInfoCollection scriptInfoCollection(*info.m_pInfoCollection);
						scriptInfoCollection.m_actor = scriptInfoCollection.m_actor;
						scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
						scriptInfoCollection.m_top = scriptInfoCollection.m_top;

						const ScriptValue argv[] = { ScriptValue(pState), ScriptValue(&scriptInfoCollection), ScriptValue(info.m_pAnimTable) };

						const DC::ScriptLambda* pLambda = pState->m_phaseAnimFunc;
						VALIDATE_LAMBDA(pLambda, "anim-state-phase-anim-func", m_animState.m_name.m_symbol);
						animId = ScriptManager::Eval(pLambda, SID("anim-state-phase-anim-func"), ARRAY_COUNT(argv), argv).m_stringId;
					}

					if (info.m_pOverlaySnapshot)
					{
						animId = info.m_pOverlaySnapshot->LookupTransformedAnimId(animId);
					}

					if (info.m_pAnimTable && info.m_pAnimTable->LookupAnim(animId).ToArtItem())
					{
						return true;
					}
				}
				else
				{
					return true;
				}
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTransition* AnimTransitionGroupGetByName(const DC::AnimTransitionGroup* pGroup, StringId64 transitionId)
{
	const U32F numTransitions = pGroup->m_count;

	if (0 == numTransitions)
		return nullptr;

	const DC::AnimTransition* pTransitions = pGroup->m_array;
	for (U32F i = 0; i < numTransitions; ++i)
	{
		if (pTransitions[i].m_flags & DC::kAnimTransitionFlagGroup)
		{
			const DC::AnimTransitionGroup* pTransitionGroup = pTransitions[i].m_group;
			const DC::AnimTransition* pFound = AnimTransitionGroupGetByName(pTransitionGroup, transitionId);
			if (pFound)
				return pFound;
		}
		else
		{
			if (pTransitions[i].m_name == transitionId)
				return &pTransitions[i];
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTransition* AnimTransitionGroupGetByState(const DC::AnimTransitionGroup* pGroup, const DC::AnimState* state, StringId64 destId)
{
	const U32F numTransitions = pGroup->m_count;

	if (0 == numTransitions)
		return nullptr;

	const DC::AnimTransition* pTransitions = pGroup->m_array;
	for (U32F i = 0; i < numTransitions; ++i)
	{
		if (pTransitions[i].m_flags & DC::kAnimTransitionFlagGroup)
		{
			const DC::AnimTransitionGroup* pTransitionGroup = pTransitions[i].m_group;
			const DC::AnimTransition* pFound = AnimTransitionGroupGetByState(pTransitionGroup, state, destId);
			if (pFound)
				return pFound;
		}
		else
		{
			if (pTransitions[i].m_state == state && CanPathfindThrough(&pTransitions[i], destId))
				return &pTransitions[i];
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
///  return true if the transition can be taken this frame
bool AnimTransitionActive(const DC::AnimTransition* pTrans, const TransitionQueryInfo& info)
{
	if (pTrans->m_flags & DC::kAnimTransitionFlagValidOnTopInstanceOnly)
	{
		if (!info.m_isTopIntance)
			return false;
	}

	if (pTrans->m_flags & DC::kAnimTransitionFlagValidIfHasFreeInstance)
	{
		if (!info.m_hasFreeInstance)
			return false;
	}

	const DcArray<const DC::AnimTransitionCondition*>* pConditionArray = &pTrans->m_conditionArray;
	const U32F numConditions = pConditionArray->GetSize();
	if (numConditions > 0)
	{
		const DC::AnimInfoCollection transInfoCollection(*info.m_pInfoCollection);
		
		const DC::AnimTransitionCondition* const* pConditionTable = pConditionArray->GetData();

		// if all conditions are true, return this transition
		for (U32F i = 0; i < numConditions; ++i)
		{
			//			MsgOut("## Anim ## - Testing Condition\n");

			const DC::AnimTransitionCondition* pCondition = pConditionTable[i];
			
			// test builtin conditional
			float value;
			switch (pCondition->m_type)
			{
			case DC::kAnimTransitionConditionTypeUsePhase:
				{
					// This is used to allow an animator to say 'Be fully blended out at the end of the animation'.
					// We do this by starting the blend 'blendTime' seconds earlier.
					float fadeTime = pTrans->m_fadeTime;

					if (info.m_pStateBlendOverlay != nullptr)
					{
						const StringId64 destStateId = pTrans->m_state->m_name.m_symbol;
						const DC::BlendOverlayEntry* pEntry = info.m_pStateBlendOverlay->Lookup(info.m_stateId, destStateId);
						if (pEntry != nullptr && pEntry->m_animFadeTime >= 0.0f)
						{
							if (pEntry->m_allowAutoTransition || pTrans->m_name != SID("auto"))
								fadeTime = pEntry->m_animFadeTime;
						}
					}

					if (pTrans->m_flags & DC::kAnimTransitionFlagStartBlendEarly)
					{
						const float adjustedPhase = info.m_phase + (fadeTime * info.m_updateRate);
						value = Limit01(adjustedPhase);
					}
					else if (pTrans->m_flags & DC::kAnimTransitionFlagStartBlendEarlyReverse)
					{
						const float adjustedPhase = info.m_phase + (fadeTime * info.m_updateRate * -1.f);
						value = Limit01(adjustedPhase);
					}
					else
					{
						value = info.m_phase;
					}
				}
				break;
			case DC::kAnimTransitionConditionTypeUseFrame:
				value = info.m_frame;
				break;
			case DC::kAnimTransitionConditionTypeUseFacingDiff:
				value = transInfoCollection.m_top->m_facingDiff;
				break;
			case DC::kAnimTransitionConditionTypeUseMoveAngle:
				value = transInfoCollection.m_top->m_moveAngle;
				break;
			case DC::kAnimTransitionConditionTypeUseMoveAngleChange:
				value = transInfoCollection.m_top->m_moveAngleChange;
				break;
			case DC::kAnimTransitionConditionTypeUseEvadeAngle:
				value = transInfoCollection.m_actor->m_evadeAngle;
				break;
			case DC::kAnimTransitionConditionTypeUseRandomNumber:
				value = transInfoCollection.m_actor->m_randomNumber;
				break;
			case DC::kAnimTransitionConditionTypeUseStateFade:
				value = info.m_stateFade;
				break;
			default:
				ANIM_ASSERT(false);
				value = 0.0f;
				break;
			}

			if (!RangeActive(pCondition->m_range, value))
			{
				return false;
			}
			
			if (pCondition->m_flags & DC::kAnimTransitionConditionFlagCheckTopInfoTransitionSwitch)
			{
				if (!transInfoCollection.m_top->m_transitionSwitch)
					return false;
			}

			// test any lambda conditional
			if (pCondition->m_flags & DC::kAnimTransitionConditionFlagUseLambda)
			{
				const DC::AnimTransitionConditionLambda* pCondLmb = static_cast<const DC::AnimTransitionConditionLambda*>(pCondition);
				const DC::ScriptLambda* pLmb = pCondLmb->m_lambda;

				const ScriptValue argv[] =
				{ 
					ScriptValue(&transInfoCollection),
					ScriptValue(info.m_frame),
					ScriptValue(info.m_phaseAnimId),
					ScriptValue(info.m_phaseAnimLooping)
				};

				bool active = ScriptManager::Eval(pLmb, SID("anim-transition-cond-func"), ARRAY_COUNT(argv), argv).m_int32!=0;
				if (!active)
					return false;
			}
		}
	}

	if (pTrans->m_state && (pTrans->m_flags & DC::kAnimTransitionFlagCheckPhaseAnimExists))
	{
		const DC::AnimState* pState = pTrans->m_state;
		const AnimTable* pAnimTable = info.m_pAnimTable;
		const AnimOverlaySnapshot* pOverlays = info.m_pOverlaySnapshot;
		const StringId64 baseAnimId = pState->m_phaseAnimName;
		StringId64 animId = baseAnimId;

		if (pState->m_phaseAnimFunc && info.m_pInfoCollection)
		{
			PROFILE(Animation, ATA_PhaseAnimFunc);

			DC::AnimInfoCollection scriptInfoCollection(*info.m_pInfoCollection);
			scriptInfoCollection.m_actor = scriptInfoCollection.m_actor;
			scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
			scriptInfoCollection.m_top = scriptInfoCollection.m_top;

			const ScriptValue argv[] = { ScriptValue(pState), ScriptValue(&scriptInfoCollection), ScriptValue(pAnimTable) };

			const DC::ScriptLambda* pLambda = pState->m_phaseAnimFunc;
			VALIDATE_LAMBDA(pLambda, "anim-state-phase-anim-func", m_animState.m_name.m_symbol);
			animId = ScriptManager::Eval(pLambda, SID("anim-state-phase-anim-func"), ARRAY_COUNT(argv), argv).m_stringId;
		}

		if (pOverlays)
		{
			animId = pOverlays->LookupTransformedAnimId(animId);
		}

		if (nullptr == pAnimTable->LookupAnim(animId).ToArtItem())
			return false;
	}

	return true;
}
	
/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTransition* AnimTransitionGroupGetActiveByName(const DC::AnimTransitionGroup* pGroup, StringId64 transitionId, const TransitionQueryInfo& info)
{
	if (!pGroup)
		return nullptr;

	// Check all transitions
	const U32F numTransitions = pGroup->m_count;
	if (0 == numTransitions)
		return nullptr;

	const DC::AnimTransition* pTransitions = pGroup->m_array;
	for (U32F i = 0; i < numTransitions; ++i)
	{
		// If this is a group, recurse
		if (pTransitions[i].m_flags & DC::kAnimTransitionFlagGroup)
		{
			const DC::AnimTransitionGroup* pTransitionGroup = pTransitions[i].m_group;
			const DC::AnimTransition* pFound = AnimTransitionGroupGetActiveByName(pTransitionGroup, transitionId, info);
			if (pFound)
				return pFound;
		}
		
		// If the name matches the transition we check conditions
		else if ((pTransitions[i].m_name == transitionId))
		{
			if (pTransitions[i].m_state == nullptr)
			{
				MsgAnim("Destination state for transition (%s) does not exist!\n", DevKitOnly_StringIdToString(pTransitions[i].m_name));
				return nullptr;
			}
			
			if (AnimTransitionActive(&pTransitions[i], info))
			{
				return &pTransitions[i];
			}
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTransition* AnimTransitionGroupGetActiveByState(const DC::AnimTransitionGroup* pGroup, StringId64 stateId, const TransitionQueryInfo& info)
{
	const U32F numTransitions = pGroup->m_count;
	if (0 == numTransitions)
		return nullptr;

	// Check all transitions
	const DC::AnimTransition* pTransitions = pGroup->m_array;
	for (U32F i = 0; i < numTransitions; ++i)
	{
		// If this is a group, recurse
		if (pTransitions[i].m_flags & DC::kAnimTransitionFlagGroup)
		{
			const DC::AnimTransitionGroup* pTransitionGroup = pTransitions[i].m_group;
			const DC::AnimTransition* pFound = AnimTransitionGroupGetActiveByState(pTransitionGroup, stateId, info);
			if (pFound)
				return pFound;
		}

		// If the name matches the transition we check conditions
		else if ((pTransitions[i].m_state != nullptr && pTransitions[i].m_state->m_name.m_symbol == stateId) &&
				 (AnimTransitionActive(&pTransitions[i], info)))
		{
			return &pTransitions[i];
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTransition* AnimStateTransitionGetByFinalState(const DC::AnimState* pState, StringId64 destStateId, const DC::AnimInfoCollection* pInfoCollection)
{
	const DC::AnimState* pDestState = nullptr;

	{
		ScopedTempAllocator scopedAlloc(FILE_LINE_FUNC);

		AnimTransitionSearch* pSearcher = NDI_NEW AnimTransitionSearch;
		if (!pSearcher)
			return nullptr;

		pDestState = pSearcher->Search(pState, destStateId, pInfoCollection);
	}

	const DC::AnimTransition* pTrans = pState->m_dynamicTransitionGroup ? AnimTransitionGroupGetByState(pState->m_dynamicTransitionGroup, pDestState, destStateId) : nullptr;
	if (pTrans == nullptr)
		return AnimTransitionGroupGetByState(pState->m_transitionGroup, pDestState, destStateId);
	else
		return pTrans;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// AnimState
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTransition* AnimStateGetTransitionByFinalState(const DC::AnimState* pState, StringId64 destStateId, const DC::AnimInfoCollection* pInfoCollection, IAnimTransitionSearch* pSearcher)
{
	const DC::AnimState* pDestState = nullptr;

	if (pSearcher)
	{
		pDestState = pSearcher->Search(pState, destStateId, pInfoCollection);
	}
	else
	{
		ScopedTempAllocator scopedAlloc(FILE_LINE_FUNC);

		pSearcher = NDI_NEW AnimTransitionSearch;
		if (!pSearcher)
			return nullptr;

		pDestState = pSearcher->Search(pState, destStateId, pInfoCollection);
	}

	const DC::AnimTransition* pTrans = pState->m_dynamicTransitionGroup ? AnimTransitionGroupGetByState(pState->m_dynamicTransitionGroup, pDestState, destStateId) : nullptr;
	if (pTrans == nullptr)
		return AnimTransitionGroupGetByState(pState->m_transitionGroup, pDestState, destStateId);
	else
		return pTrans;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTransition* AnimStateGetActiveTransitionByName(const DC::AnimState* pState, StringId64 transitionId, const TransitionQueryInfo& info)
{
	if (!pState)
		return nullptr;

	const DC::AnimTransition* pTrans = nullptr;

	if (!pTrans && pState->m_dynamicTransitionGroup)
	{
		// If the dynamic transition is valid we don't want to check the static transitions
		// Maaan.... this is pretty nasty. Should be refactored to a 'Transition Table' something rather... CGY
		const DC::AnimTransitionGroup* pDynamicTransitionGroup = pState->m_dynamicTransitionGroup;
		if (AnimTransitionGroupValid(pDynamicTransitionGroup, transitionId, info))
		{
			pTrans = AnimTransitionGroupGetActiveByName(pDynamicTransitionGroup, transitionId, info);
			return pTrans;
		}
	}

	// Then look in the static one
	if (!pTrans)
	{
		const DC::AnimTransitionGroup* pTransitionGroup = pState->m_transitionGroup;
		pTrans = AnimTransitionGroupGetActiveByName(pTransitionGroup, transitionId, info);
	}

	return pTrans;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTransition* AnimStateGetActiveTransitionByState(const DC::AnimState* pState, StringId64 stateId, const TransitionQueryInfo& info)
{
	if (!pState)
		return nullptr;

	const DC::AnimTransition* pTrans = nullptr;

	if (!pTrans && pState->m_dynamicTransitionGroup)
	{
		// If the dynamic transition is valid we don't want to check the static transitions
		// Maaan.... this is pretty nasty. Should be refactored to a 'Transition Table' something rather... CGY
		const DC::AnimTransitionGroup* pDynamicTransitionGroup = pState->m_dynamicTransitionGroup;
		
		pTrans = AnimTransitionGroupGetActiveByState(pDynamicTransitionGroup, stateId, info);
		if (pTrans != nullptr)
			return pTrans;		
	}

	// Then look in the static one
	if (!pTrans)
	{
		const DC::AnimTransitionGroup* pTransitionGroup = pState->m_transitionGroup;
		pTrans = AnimTransitionGroupGetActiveByState(pTransitionGroup, stateId, info);
	}

	return pTrans;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTransition* AnimTransitionGroupGetActiveByStateFilter(const DC::AnimTransitionGroup* pGroup, IAnimStateFilter* pFilter, const TransitionQueryInfo& info)
{
	const U32F numTransitions = pGroup->m_count;
	if (0 == numTransitions)
		return nullptr;

	// Check all transitions
	const DC::AnimTransition* pTransitions = pGroup->m_array;
	for (U32F i = 0; i < numTransitions; ++i)
	{
		// If this is a group, recurse
		if (pTransitions[i].m_flags & DC::kAnimTransitionFlagGroup)
		{
			const DC::AnimTransitionGroup* pTransitionGroup = pTransitions[i].m_group;
			const DC::AnimTransition* pFound = AnimTransitionGroupGetActiveByStateFilter(pTransitionGroup, pFilter, info);
			if (pFound)
				return pFound;
		}

		// If the name matches the transition we check conditions
		else if (pTransitions[i].m_state != nullptr && pFilter->AnimStateValid(pTransitions[i].m_state) &&
			(AnimTransitionActive(&pTransitions[i], info)))
		{
			return &pTransitions[i];
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTransition* AnimStateGetActiveTransitionByStateFilter(const DC::AnimState* pState, IAnimStateFilter* pFilter, const TransitionQueryInfo& info)
{
	if (!pState)
		return nullptr;

	const DC::AnimTransition* pTrans = nullptr;

	if (!pTrans && pState->m_dynamicTransitionGroup)
	{
		// If the dynamic transition is valid we don't want to check the static transitions
		// Maaan.... this is pretty nasty. Should be refactored to a 'Transition Table' something rather... CGY
		const DC::AnimTransitionGroup* pDynamicTransitionGroup = pState->m_dynamicTransitionGroup;

		pTrans = AnimTransitionGroupGetActiveByStateFilter(pDynamicTransitionGroup, pFilter, info);
		if (pTrans != nullptr)
			return pTrans;
	}

	// Then look in the static one
	if (!pTrans)
	{
		const DC::AnimTransitionGroup* pTransitionGroup = pState->m_transitionGroup;
		pTrans = AnimTransitionGroupGetActiveByStateFilter(pTransitionGroup, pFilter, info);
	}

	return pTrans;
}

/// --------------------------------------------------------------------------------------------------------------- ///
///  return true if the transition was found
bool AnimStateTransitionExists(const DC::AnimState* pState, StringId64 transitionId, const TransitionQueryInfo& info)
{
	if (!pState)
		return false;

	bool valid = false;

	if (!valid && pState->m_dynamicTransitionGroup)
	{
		const DC::AnimTransitionGroup* pDynamicTransitionGroup = pState->m_dynamicTransitionGroup;
		valid = AnimTransitionGroupExists(pDynamicTransitionGroup, transitionId, info);
	}

	if (!valid)
	{
		const DC::AnimTransitionGroup* pTransitionGroup = pState->m_transitionGroup;
		valid = AnimTransitionGroupExists(pTransitionGroup, transitionId, info);
	}

	return valid;
}


/// --------------------------------------------------------------------------------------------------------------- ///
///  return true if the transition was found and can be taken
bool AnimStateTransitionValid(const DC::AnimState* pState, StringId64 transitionId, const TransitionQueryInfo& info)
{
	if (!pState)
		return false;

	bool valid = false;

	if (!valid && pState->m_dynamicTransitionGroup)
	{
		const DC::AnimTransitionGroup* pDynamicTransitionGroup = pState->m_dynamicTransitionGroup;
		valid = AnimTransitionGroupValid(pDynamicTransitionGroup, transitionId, info);
	}

	if (!valid)
	{
		const DC::AnimTransitionGroup* pTransitionGroup = pState->m_transitionGroup;
		valid = AnimTransitionGroupValid(pTransitionGroup, transitionId, info);
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimTransitionGroupHasId(const DC::AnimTransitionGroup* pGroup, StringId64 transitionId)
{
	if ((!pGroup) || (pGroup->m_count == 0) || (!pGroup->m_array))
		return false;

	const U32F numTransitions = pGroup->m_count;
	if (numTransitions)
	{
		const DC::AnimTransition* pTransitions = pGroup->m_array;
		for (U32F i = 0; i < numTransitions; ++i)
		{
			if (pTransitions[i].m_flags & DC::kAnimTransitionFlagGroup)
			{
				const DC::AnimTransitionGroup* pTransitionGroup = pTransitions[i].m_group;
				if (AnimTransitionGroupHasId(pTransitionGroup, transitionId))
					return true;
			}
			else if (pTransitions[i].m_name == transitionId)
			{
				return true;
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateHasTransitionId(const DC::AnimState* pState, StringId64 transitionId)
{
	if (!pState)
		return false;

	bool valid = false;

	if (!valid && pState->m_dynamicTransitionGroup)
	{
		const DC::AnimTransitionGroup* pDynamicTransitionGroup = pState->m_dynamicTransitionGroup;
		valid = AnimTransitionGroupHasId(pDynamicTransitionGroup, transitionId);
	}

	if (!valid)
	{
		const DC::AnimTransitionGroup* pTransitionGroup = pState->m_transitionGroup;
		valid = AnimTransitionGroupHasId(pTransitionGroup, transitionId);
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTransition* AnimStateGetTransitionByName( const DC::AnimState* pState, StringId64 transitionId )
{
	if (!pState)
		return nullptr;

	const DC::AnimTransition* pResult = nullptr;

	if (!pResult && pState->m_dynamicTransitionGroup)
	{
		const DC::AnimTransitionGroup* pDynamicTransitionGroup = pState->m_dynamicTransitionGroup;
		pResult = AnimTransitionGroupGetByName(pDynamicTransitionGroup, transitionId);
	}

	if (!pResult)
	{
		const DC::AnimTransitionGroup* pTransitionGroup = pState->m_transitionGroup;
		pResult = AnimTransitionGroupGetByName(pTransitionGroup, transitionId);
	}

	return pResult;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateUserIdFilter::AnimStateUserIdFilter(StringId64 userId)
	: m_userId(userId)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateUserIdFilter::AnimStateValid(const DC::AnimState* pState)
{
	return pState->m_userId == m_userId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static int BlendOverlayKeyCompare(const DC::BlendOverlayKey& key1, const DC::BlendOverlayKey& key2)
{
	if (key1.m_srcId.GetValue() < key2.m_srcId.GetValue())
		return -1;
	else if (key1.m_srcId.GetValue() > key2.m_srcId.GetValue())
		return +1;
	else
	{
		if (key1.m_dstId.GetValue() < key2.m_dstId.GetValue())
			return -1;
		else if (key1.m_dstId.GetValue() > key2.m_dstId.GetValue())
			return +1;
	}
	return 0;
}

static bool BlendOverlayKeyIsEqual(const DC::BlendOverlayKey& key1, const DC::BlendOverlayKey& key2)
{
	return BlendOverlayKeyCompare(key1, key2) == 0;
}

static bool BlendOverlayKeyIsLess(const DC::BlendOverlayKey& key1, const DC::BlendOverlayKey& key2)
{
	return BlendOverlayKeyCompare(key1, key2) < 0;
}

static bool BlendOverlayKeyIsGreater(const DC::BlendOverlayKey& key1, const DC::BlendOverlayKey& key2)
{
	return BlendOverlayKeyCompare(key1, key2) > 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// BlendOverlayMapLookupIndex the same as MapLookupIndex, however there's no easy way we can just use MapLookupIndex when comparing DC::BlendOverlayKey
/// --------------------------------------------------------------------------------------------------------------- ///
static int BlendOverlayMapLookupIndex(const DC::BlendOverlayMap* pMap, const DC::BlendOverlayKey& key)
{
	if (!pMap)
		return -1;

	if (pMap->m_count == 0)
		return -1;

	const I32F count = pMap->m_count;

	const DC::BlendOverlayKey* pKeys = pMap->m_keys;
	const DC::MapData* pData = pMap->m_data;

	// do a binary search of the map (it's sorted by key)
	I32F low = 0;
	I32F high = count - 1;

	while (low <= high)
	{
		const I32F mid = (low + high) / 2;
		const DC::BlendOverlayKey midVal = pKeys[mid];

		if (BlendOverlayKeyIsLess(midVal, key))
			low = mid + 1;
		else if (BlendOverlayKeyIsGreater(midVal, key))
			high = mid - 1;
		else
			return mid;
	}
	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::BlendOverlayEntry* BlendOverlay::Lookup(const DC::BlendOverlayMap* pMap, const DC::BlendOverlayKey& key)
{
	const int index = BlendOverlayMapLookupIndex(pMap, key);
	const DC::MapData* const pData = pMap->m_data;
	return (index >= 0) ? static_cast<const DC::BlendOverlayEntry*>(pData[index].m_ptr) : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::BlendOverlayEntry* BlendOverlay::Lookup(StringId64 srcId, StringId64 dstId) const
{
	const DC::BlendOverlayEntry* pEntry = nullptr;

	DC::BlendOverlayKey key;
	key.m_srcId = srcId;
	key.m_dstId = dstId;

	if (m_pDefaultOverlay)
	{
		pEntry = Lookup(m_pDefaultOverlay, key);
	}

	if (m_pCustomOverlay)
	{
		pEntry = Lookup(m_pCustomOverlay, key);
	}

	return pEntry;
}
