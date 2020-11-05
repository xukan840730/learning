/*
 * Copyright (c) 2011 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-transition-search.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/scriptx/h/dc-types.h"

/// --------------------------------------------------------------------------------------------------------------- ///
AnimTransitionSearch::AnimTransitionSearch()
{
	m_stateNodeFreeList = nullptr;
	m_stateNodeOpenList = nullptr;
	m_stateNodeClosedList = nullptr;
	for (I32F ii = 0; ii < kNodeCount; ++ii)
	{
		m_stateNodes[ii].m_next = m_stateNodeFreeList;
		m_stateNodeFreeList = &m_stateNodes[ii];;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimState* AnimTransitionSearch::Search(const DC::AnimState* pState,
												  StringId64 destStateId,
												  const DC::AnimInfoCollection* pInfoCollection)
{
	m_fromState = pState->m_name.m_string;
	m_destState = destStateId;
	if (pState->m_dynamicTransitionGroup)
		AddOpenNodesFromGroup(pState->m_dynamicTransitionGroup, nullptr);
	if (pState->m_transitionGroup)
		AddOpenNodesFromGroup(pState->m_transitionGroup, nullptr);
	while (m_stateNodeOpenList)
	{
		StateNode * node = m_stateNodeOpenList;
		m_stateNodeOpenList = m_stateNodeOpenList->m_next;

		if (node->m_state->m_name.m_symbol == destStateId)
		{
			while (node->m_parent)
				node = node->m_parent;

			return node->m_state;
		}
		RefreshStateDynamicTransitionGroups(node->m_state, pInfoCollection);
		if (node->m_state->m_dynamicTransitionGroup)
			AddOpenNodesFromGroup(node->m_state->m_dynamicTransitionGroup, node);
		AddOpenNodesFromGroup(node->m_state->m_transitionGroup, node);

		AddToClosedList(node);
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimTransitionSearch::AddOpenNode( const DC::AnimState* state, StateNode* parent )
{
	StateNode * node = nullptr;
	for (StateNode * find = m_stateNodeOpenList; find != nullptr; find = find->m_next)
	{
		if (find->m_state == state)
		{
			node = find;
			break;
		}
	}

	if (!node)
	{
		if (!m_stateNodeFreeList)
		{
			MsgErr("ran out of free nodes in searches, please talk to a programmer (From: %s To: %s)", m_fromState, DevKitOnly_StringIdToString(m_destState));
			ANIM_ASSERT(false);
			return; // increase kNodeCount if hit
		}

		// put this node at the end of our list, not at the beginning
		StateNode* endOfList = m_stateNodeOpenList;
		while (endOfList != nullptr && endOfList->m_next != nullptr)
		{
			endOfList = endOfList->m_next;
		}

		node = m_stateNodeFreeList;
		m_stateNodeFreeList = m_stateNodeFreeList->m_next;
		node->m_state = state;
		node->m_cost = 0;
		node->m_parent = parent;
		node->m_next = nullptr;
		if (endOfList != nullptr)
		{
			endOfList->m_next = node;
		}
		else
		{
			// the list was empty, put it at the beginning
			m_stateNodeOpenList = node;
		}
	}
	else if (parent && parent->m_cost+1 < node->m_cost)
	{
		node->m_cost = parent->m_cost+1;
		node->m_parent = parent;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimTransitionSearch::IsOnClosedList( const DC::AnimState * state )
{
	for (StateNode * find = m_stateNodeClosedList; find != nullptr; find = find->m_next)
	{
		if (find->m_state == state)
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimTransitionSearch::AddOpenNodesFromTransition( const DC::AnimTransition* pTrans, StateNode* pParentNode )
{
	if (IsOnClosedList(pTrans->m_state) || !CanPathfindThrough(pTrans, m_destState) || !AllowPathFindThrough(pTrans))
		return;

	if (pTrans->m_flags & DC::kAnimTransitionFlagGroup)
	{
		ANIM_ASSERT(pTrans->m_group);
		AddOpenNodesFromGroup(pTrans->m_group, pParentNode);
	}
	else
		AddOpenNode(pTrans->m_state, pParentNode);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimTransitionSearch::AddOpenNodesFromGroup( const DC::AnimTransitionGroup* group, StateNode* pParentNode )
{
	const U32F numTransitions = group->m_count;
	const DC::AnimTransition* pTransitions = group->m_array;
	for (U32F i = 0; i < numTransitions; ++i)
	{
		AddOpenNodesFromTransition(&pTransitions[i], pParentNode);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimTransitionSearch::AddToClosedList( StateNode * node )
{
	node->m_next = m_stateNodeClosedList;
	m_stateNodeClosedList = node;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimTransitionSearch::RefreshStateDynamicTransitionGroups( const DC::AnimState* pState, const DC::AnimInfoCollection* pInfoCollection )
{
	AnimDcAssertNotRelocatingJanitor dcnrj(FILE_LINE_FUNC);

	pState->m_dynamicTransitionGroup = nullptr;

	if (pState->m_dynamicTransitionGroupFunc)
	{
		DC::AnimInfoCollection scriptInfoCollection(*pInfoCollection);
		scriptInfoCollection.m_actor = scriptInfoCollection.m_actor;
		scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
		scriptInfoCollection.m_top = scriptInfoCollection.m_top;

		const ScriptValue argv[] = { ScriptValue(pState), ScriptValue(&scriptInfoCollection) };

		const DC::ScriptLambda* pLambda = pState->m_dynamicTransitionGroupFunc;
		const StringId64 transitionGroupId = ScriptManager::Eval(pLambda, SID("anim-state-dtgroup-func"), ARRAY_COUNT(argv), argv).m_stringId;

		const DC::AnimTransitionGroup* pTransGroup = ScriptManager::MapLookup<DC::AnimTransitionGroup>(pState->m_actor->m_transitionGroupMap, transitionGroupId);
		if (pTransGroup)
		{
			pState->m_dynamicTransitionGroup = pTransGroup;
		}
	}
}
