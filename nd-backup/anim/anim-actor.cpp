/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-actor.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-mgr.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/scriptx/h/code.h"
#include "ndlib/scriptx/h/dc-types.h"

/// --------------------------------------------------------------------------------------------------------------- ///
typedef MapArray<StringId64, const DC::AnimState*> DcAnimStateMap;

static bool AnimTransitionGroupLogin(const DC::AnimTransitionGroup* group, const DC::AnimActor* actor);

/// --------------------------------------------------------------------------------------------------------------- ///
static const DC::AnimState* AnimActorFindStateInternal(const DC::AnimActor* actor, StringId64 stateId)
{
	PROFILE(Animation, AnimActorFindStateInternal);

	GAMEPLAY_ASSERT(actor && actor->m_stateMapArray);

	if (0)
	{
#if !FINAL_BUILD
		for (int i=0; i<actor->m_stateGroupCount; i++)
		{
			const DC::ActorStateList* pList = ScriptManager::Lookup<const DC::ActorStateList>(actor->m_stateGroups[i], nullptr);

			int debug = 0;
		}
#endif
	}

	const DcAnimStateMap* pStateMap = static_cast<const DcAnimStateMap*>(actor->m_stateMapArray);
	auto it = pStateMap->Find(stateId);

	return it == pStateMap->End() ? nullptr : it->second;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ValidateTransitionConditions(const DC::AnimTransition* pTrans)
{
	const DcArray<const DC::AnimTransitionCondition*>* pConditionArray = &pTrans->m_conditionArray;

	for (const DC::AnimTransitionCondition* pCondition : *pConditionArray)
	{
		if (pCondition->m_flags & DC::kAnimTransitionConditionFlagUseLambda)
		{
			const DC::AnimTransitionConditionLambda* pCondLmb = static_cast<const DC::AnimTransitionConditionLambda*>(pCondition);
			const DC::ScriptLambda* pLmb = pCondLmb->m_lambda;
			if (pLmb->m_id != SID("anim-transition-cond-func"))
			{
				return false;
			}
		}
	}
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool AnimTransitionLogin(const DC::AnimTransition* pTrans, const DC::AnimActor* pActor)
{
	if (!pTrans || !pActor)
		return false;

	if (((pTrans->m_flags & DC::kAnimTransitionFlagSpawnOnNewTrack) != 0) && ((pTrans->m_flags & DC::kAnimTransitionFlagSpawnOnSameTrack) != 0))
	{
		MsgAnim("AnimTransitionLogin: Transition '%s' has both spawn on new track and spawn on same track flags set\n", DevKitOnly_StringIdToString(pTrans->m_name));
		return false;
	}

	if (!ValidateTransitionConditions(pTrans))
	{
		MsgAnim("AnimTransitionLogin: Transition '%s' has invalid condition\n", DevKitOnly_StringIdToString(pTrans->m_name));
		return false;
	}

	if (pTrans->m_flags & DC::kAnimTransitionFlagGroup)
	{
		const DC::AnimTransitionGroup* pGroup = ScriptManager::MapLookup<DC::AnimTransitionGroup>(pActor->m_transitionGroupMap, pTrans->m_id);
		ANIM_ASSERT(pTrans->m_group == nullptr || pTrans->m_group == pGroup);

		const_cast<DC::AnimTransition*>(pTrans)->m_group = pGroup;

		if (!pTrans->m_group)
		{
			MsgAnim("AnimTransitionLogin: Transition Group '%s' could not be found\n", DevKitOnly_StringIdToString(pTrans->m_id));
			return false;
		}

		if (!AnimTransitionGroupLogin(pTrans->m_group, pActor))
			return false;
	}
	else
	{
		const DC::AnimState* pState = AnimActorFindStateInternal(pActor, pTrans->m_id);
		if (!pState)
		{
			MsgAnim("AnimTransitionGroupLogin: Could not find state '%s' for transition '%s'\n", DevKitOnly_StringIdToString(pTrans->m_id), DevKitOnly_StringIdToString(pTrans->m_name));
			return false;
		}
		//ANIM_ASSERT(pTrans->m_state == nullptr || pTrans->m_state == pState);
		const_cast<DC::AnimTransition*>(pTrans)->m_state = pState;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool AnimTransitionGroupLogin(const DC::AnimTransitionGroup* group, const DC::AnimActor* actor)
{
	ANIM_ASSERT(group);
	ANIM_ASSERT(group->m_array);
	ANIM_ASSERT(actor);

	const U32F numTransitions = group->m_count;
	if (numTransitions)
	{
		for (I32F ii = 0; ii < group->m_count; ++ii)
		{
			const DC::AnimTransition* pTrans = &group->m_array[ii];

			if (!AnimTransitionLogin(pTrans, actor))
				return false;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool AnimStateLogin(const DC::AnimState* state, const DC::AnimActor* actor)
{
	if (state->m_flags & DC::kAnimStateFlagLoggedIn)
		return true;

	ANIM_ASSERT(state->m_actor == nullptr || state->m_actor == actor);
	const_cast<DC::AnimState*>(state)->m_actor = actor;

	// login the transitions
	if (state->m_transitionGroup)
	{
		if (!AnimTransitionGroupLogin(state->m_transitionGroup, state->m_actor))
		{
			MsgAnim("AnimStateLogin: Could not login transition group properly for state %s!!\n",
					state->m_name.m_string.GetString());
			return false;
		}
	}

	state->m_flags |= DC::kAnimStateFlagLoggedIn;
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimActorLogin(const DC::AnimActor* actor)
{
	PROFILE(Animation, AnimActorLogin);

	if (actor == nullptr || actor->m_stateMapArray == nullptr)
		return false;

	// Login all states
	AnimActorStateIterator itState(actor);
	while (!itState.AtEnd())
	{
		const DC::AnimState* pState = itState.GetState();
		if (!AnimStateLogin(pState, actor))
			return false;
		itState.Advance();
	}

	// Login all dynamic transition groups
	const DC::Map* pTransMap = actor->m_transitionGroupMap;
	for (U32F i = 0; i < pTransMap->m_count; ++i)
	{
		AnimTransitionGroupLogin((const DC::AnimTransitionGroup*)pTransMap->m_data[i].m_ptr, actor);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimState* AnimActorFindState(const DC::AnimActor* actor, StringId64 stateId)
{
	const DC::AnimState* pState =  AnimActorFindStateInternal(actor, stateId);
	if (pState)
	{
		ANIM_ASSERT(pState->m_flags & DC::kAnimStateFlagLoggedIn);
		return pState;
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int AnimActorStateCount(const DC::AnimActor* actor)
{
	int numStates = actor->m_stateCount;
	for (int i = 0; i < actor->m_stateGroupCount; i++)
	{
		const DC::ActorStateList* pGroup = ScriptManager::Lookup<const DC::ActorStateList>(actor->m_stateGroups[i], nullptr);
		if (pGroup)
		{
			numStates += pGroup->m_stateCount;
		}
	}

	return numStates;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimActorScriptObserver : public ScriptObserver
{
	struct AnimActorSetupData
	{
		StringId64 m_name = INVALID_STRING_ID_64;
		DcAnimStateMap* m_pStateMapArray = nullptr;
	};

	static const int kMaxAnimActors = 16;
	AnimActorSetupData m_animActorList[kMaxAnimActors];
	int m_numAnimActors = 0;

	bool m_initialized = false;

	AnimActorScriptObserver() : ScriptObserver(FILE_LINE_FUNC, INVALID_STRING_ID_64)
	{
	}

	virtual void OnSymbolImported(StringId64 moduleId,
								  StringId64 symbol,
								  StringId64 type,
								  const void* pData,
								  const void* pOldData,
								  Memory::Context allocContext)
	{
		if (type == SID("anim-actor"))
		{
			int iAnimActor;
			for (iAnimActor = 0; iAnimActor < m_numAnimActors; iAnimActor++)
			{
				if (symbol == m_animActorList[iAnimActor].m_name)
					break;
			}

			bool found = iAnimActor < m_numAnimActors;

			// Should be in list if and only if we're past initialization
			GAMEPLAY_ASSERT(found == m_initialized);

			if (!found)
			{
				GAMEPLAY_ASSERTF(m_numAnimActors < kMaxAnimActors, ("Increase kMaxAnimActors"));

				m_animActorList[m_numAnimActors].m_name = symbol;
				m_animActorList[m_numAnimActors].m_pStateMapArray = nullptr;
				m_numAnimActors++;
			}
			else
			{
				const DC::AnimActor* pAnimActor = static_cast<const DC::AnimActor*>(pData);

				GenerateStateMap(pAnimActor, iAnimActor);
				bool success = AnimActorLogin(pAnimActor);
				GAMEPLAY_ASSERT(success);

				EngineComponents::GetAnimMgr()->ForAllUsedAnimData(AnimControlScriptUpdateFunctor, 0);
			}
		}
		else if (type == SID("actor-state-list"))
		{
			if (m_initialized)
			{
				const DC::ActorStateList* pStateList = static_cast<const DC::ActorStateList*>(pData);
				int iAnimActor;
				for (iAnimActor = 0; iAnimActor < m_numAnimActors; iAnimActor++)
				{
					if (pStateList->m_actorId == m_animActorList[iAnimActor].m_name)
						break;
				}

				bool found = iAnimActor < m_numAnimActors;
				GAMEPLAY_ASSERT(found);

				const DC::AnimActor* pAnimActor = ScriptManager::Lookup<DC::AnimActor>(pStateList->m_actorId);

				GenerateStateMap(pAnimActor, iAnimActor);
				bool success = AnimActorLogin(pAnimActor);
				GAMEPLAY_ASSERT(success);

				EngineComponents::GetAnimMgr()->ForAllUsedAnimData(AnimControlScriptUpdateFunctor, 0);
			}
		}
	}

	virtual void OnSymbolRelocated(StringId64 moduleId,
								   StringId64 symbol,
								   StringId64 type,
								   const void* pOldData,
								   ptrdiff_t deltaPos,
								   uintptr_t lowerBound,
								   uintptr_t upperBound,
								   Memory::Context allocContext)
	{
	}

	virtual void OnSymbolUnloaded(StringId64 moduleId,
								  StringId64 symbol,
								  StringId64 type,
								  const void* pData,
								  Memory::Context allocContext)
	{
	}

	virtual void OnModuleImported(StringId64 moduleId, Memory::Context allocContext) {}

	virtual void OnModuleRelocated(StringId64 moduleId,
								   Memory::Context allocContext,
								   ptrdiff_t deltaPos,
								   uintptr_t lowerBound,
								   uintptr_t upperBound)
	{
	}

	virtual void OnModuleRemoved(StringId64 moduleId, Memory::Context allocContext) {}

	void GenerateStateMap(const DC::AnimActor* pAnimActor, int actorListIndex)
	{
		DcAnimStateMap* pPrevStateMap = m_animActorList[actorListIndex].m_pStateMapArray;
		DcAnimStateMap* pStateMap = nullptr;

		int stateCount = AnimActorStateCount(pAnimActor);
		if (pPrevStateMap && pPrevStateMap->GetCapacity() >= stateCount)
		{
			pPrevStateMap->Clear();
			pStateMap = pPrevStateMap;
		}
		else
		{
			if (pPrevStateMap)
			{
				pPrevStateMap->Reset();
				NDI_DELETE pPrevStateMap;
			}

			pStateMap = NDI_NEW(kAllocScriptData) DcAnimStateMap;
			pStateMap->Init(stateCount, kAllocScriptData, FILE_LINE_FUNC);
		}

		AnimActorStateIterator itState(pAnimActor);
		while (!itState.AtEnd())
		{
			const DC::AnimState* pState = itState.GetState();
			pStateMap->Add(pState->m_name.m_symbol, pState);
			itState.Advance();
		}

		m_animActorList[actorListIndex].m_pStateMapArray = pStateMap;
		const_cast<DC::AnimActor*>(pAnimActor)->m_stateMapArray = pStateMap;
	}

	void InitAnimActors()
	{
		for (int iAnimActor=0; iAnimActor<m_numAnimActors; iAnimActor++)
		{
			const DC::AnimActor* pAnimActor = ScriptManager::Lookup<DC::AnimActor>(m_animActorList[iAnimActor].m_name);
			GenerateStateMap(pAnimActor, iAnimActor);
			bool success = AnimActorLogin(pAnimActor);
			GAMEPLAY_ASSERT(success);
		}

		m_initialized = true;
	}

	static void AnimControlDCUpdateFunctor(FgAnimData* pAnimData, uintptr_t data)
	{
		if (pAnimData && pAnimData->m_pAnimControl)
		{
			pAnimData->m_pAnimControl->ReloadScriptData();
		}
	}

	static void AnimControlScriptUpdateFunctor(FgAnimData* pAnimData, uintptr_t data)
	{
		if (pAnimData && pAnimData->m_pAnimControl)
		{
			pAnimData->m_pAnimControl->ReloadScriptData();
			pAnimData->m_pAnimControl->NotifyAnimTableUpdated();
		}
	}
};

static AnimActorScriptObserver s_animActorScriptObserver;

/// --------------------------------------------------------------------------------------------------------------- ///
void InitAnimActorScriptObserver()
{
	ScriptManager::RegisterObserver(&s_animActorScriptObserver);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void InitAnimActors()
{
	s_animActorScriptObserver.InitAnimActors();
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimActorStateIterator::AnimActorStateIterator(const DC::AnimActor* pAnimActor)
{
	GAMEPLAY_ASSERT(pAnimActor != nullptr);

	m_pAnimActor = pAnimActor;
	m_pCurrentGroup = nullptr;
	m_groupIndex = -1;
	m_stateIndex = -1;

	Advance();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimState* AnimActorStateIterator::GetState()
{
	if (AtEnd())
		return nullptr;

	if (m_groupIndex == -1)
	{
		GAMEPLAY_ASSERT(m_stateIndex >= 0 && m_stateIndex < m_pAnimActor->m_stateCount);
		return &m_pAnimActor->m_stateArray[m_stateIndex];
	}
	else
	{
		GAMEPLAY_ASSERT(m_pCurrentGroup && m_stateIndex >= 0 && m_stateIndex < m_pCurrentGroup->m_stateCount);
		return &m_pCurrentGroup->m_stateArray[m_stateIndex];
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimActorStateIterator::Advance()
{
	if (AtEnd())
		return;

	m_stateIndex++;

	int numStates = (m_groupIndex == -1) ? m_pAnimActor->m_stateCount : (m_pCurrentGroup) ? m_pCurrentGroup->m_stateCount : 0;

	while (m_stateIndex >= numStates)
	{
		m_groupIndex++;
		m_stateIndex = 0;

		if (m_groupIndex >= m_pAnimActor->m_stateGroupCount)
			break;

		m_pCurrentGroup = ScriptManager::Lookup<DC::ActorStateList>(m_pAnimActor->m_stateGroups[m_groupIndex], nullptr);
		numStates = (m_groupIndex == -1) ? m_pAnimActor->m_stateCount : (m_pCurrentGroup) ? m_pCurrentGroup->m_stateCount : 0;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimActorStateIterator::AtEnd()
{
	return m_groupIndex >= m_pAnimActor->m_stateGroupCount;
}
