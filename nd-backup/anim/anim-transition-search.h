/*
 * Copyright (c) 2011 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ANIM_TRANSITION_SEARCH_H
#define ANIM_TRANSITION_SEARCH_H

#include "ndlib/anim/anim-state.h"

namespace DC {
struct AnimInfoCollection;
struct AnimState;
struct AnimTransition;
struct AnimTransitionGroup;
}  // namespace DC

class AnimTransitionSearch : public IAnimTransitionSearch
{
public:
	AnimTransitionSearch();

	const DC::AnimState* Search(const DC::AnimState* pState, StringId64 destStateId, const DC::AnimInfoCollection* pInfoCollection) override;

protected:
	virtual bool AllowPathFindThrough(const DC::AnimTransition* pTrans) const { return true;}

private:
	struct StateNode
	{
		const DC::AnimState* m_state;
		StateNode* m_parent;
		I32F m_cost;
		StateNode* m_next;
	};
	static const I32F kNodeCount = 256;
	StateNode m_stateNodes[kNodeCount];
	StateNode* m_stateNodeFreeList;
	StateNode* m_stateNodeOpenList;
	StateNode* m_stateNodeClosedList;
	const char* m_fromState;
	StringId64 m_destState;	

	void AddOpenNode(const DC::AnimState* state, StateNode* parent);

	bool IsOnClosedList(const DC::AnimState * state);	

	void AddOpenNodesFromTransition(const DC::AnimTransition* pTrans, StateNode* pParentNode);

	void AddOpenNodesFromGroup(const DC::AnimTransitionGroup* group, StateNode* pParentNode);

	void AddToClosedList(StateNode * node);

	void RefreshStateDynamicTransitionGroups(const DC::AnimState* pState, const DC::AnimInfoCollection* pInfoCollection);

	
};

#endif // ANIM_TRANSITION_SEARCH_H
