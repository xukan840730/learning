/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-overlay-use-check.h"

#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/bigsort.h"

#include "ndlib/anim/anim-actor.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/scriptx/h/dc-types.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlayUseCheck::AddOverlayStatesFromTree(const DC::AnimNode* pAnimNode,
												   AnimOverlayUseCheck::OverlayStateSet& overlaystateSet)
{
	switch (pAnimNode->m_dcType.GetValue())
	{
	case SID_VAL("anim-node-animation"):
		{
			const DC::AnimNodeAnimation* pAnim = static_cast<const DC::AnimNodeAnimation*>(pAnimNode);

			// Retrieve the animation for this node
			StringId64 animationId = pAnim->m_animation;				
			overlaystateSet.Insert(OverlayState(animationId, false));						
		}
		break;

	case SID_VAL("anim-node-blend"):
		{

			const DC::AnimNodeBlend* pBlend = static_cast<const DC::AnimNodeBlend*>(pAnimNode);
			AddOverlayStatesFromTree(pBlend->m_left, overlaystateSet);
			AddOverlayStatesFromTree(pBlend->m_right, overlaystateSet);
		}
		break;
	default:
		ANIM_ASSERT(false);
		break;
	} 
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlayUseCheck::SearchForUnusedOverlayEnrties(OverlayStateSet& stateSet, OverlayEntrySet& entrySet)
{
	ScopedTempAllocator jj(FILE_LINE_FUNC);
	const U32 size = 1024*8;
	OverlayStateSet closedStates(size);
	OverlayEntrySet takenEntries(size);

	while (stateSet.Begin() != stateSet.End())
	{
		OverlayStateSet::Iterator curStateIt = stateSet.Begin();
		OverlayState curState = *curStateIt;
		stateSet.Remove(curState);
		closedStates.Insert(curState);
		{				
			ANIM_ASSERT(!stateSet.Contains(curState));
			ANIM_ASSERT(closedStates.Contains(curState));
		}
				
		//Find all entries that will affect the current state
		for (I32 iEntry = entrySet.Size() - 1; iEntry >= 0; --iEntry)
		{		
			OverlayEntryInfo& entryInfo = entrySet[iEntry];
			if (Matches(curState, entryInfo))
			{				
				//Add successor states if not already visited
				if (entryInfo.GetEntry()->m_type == DC::kAnimOverlayTypeTree)
				{
					AddOverlayStatesFromTree(entryInfo.GetEntry()->m_tree, stateSet);
				}
				else
				{					
					OverlayState newState(entryInfo.GetEntry()->m_remapId, false);
					if (!closedStates.Contains(newState))
						stateSet.Insert(newState);
				}				
				OverlayEntryInfo prevEntryInfo = entryInfo;

				//Remove from the current list since we have taken it.
				takenEntries.Insert(entryInfo);
				entrySet.Remove(entryInfo);	

				ANIM_ASSERT(!entrySet.Contains(prevEntryInfo));
				ANIM_ASSERT(takenEntries.Contains(prevEntryInfo));				
			}			
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlayUseCheck::CheckForUnusedOverlays(const DC::AnimActor* pAnimActor,
												 const DynamicAnimations& dynamicAnims,
												 const ListArray<const DC::AnimOverlaySet*>& overlays)
{
	ScopedTempAllocator jj(FILE_LINE_FUNC);
	const U32 size = 1024*10;
	OverlayStateSet overlaystateSet(size);
	OverlayEntrySet overlayEntrySet(size);
	AddOverlayStateFromAnimActors(pAnimActor, overlaystateSet);
	AddDynamicAnims(dynamicAnims, overlaystateSet);
	AddOverlaySetEntries(overlays, overlayEntrySet);
	SearchForUnusedOverlayEnrties(overlaystateSet, overlayEntrySet);
	QuickSort(overlayEntrySet.ArrayPointer(), overlayEntrySet.Size(), OverlayEntryInfo::Compare);
	MsgAnim("Entries not used:\n");
	int numunused = 0;
	for (OverlayEntrySet::Iterator entryIt = overlayEntrySet.Begin(); entryIt != overlayEntrySet.End(); ++entryIt)
	{
		numunused++;
		const OverlayEntryInfo& curEntry = *entryIt;
		MsgAnim("%s , %s , %s\n", DevKitOnly_StringIdToString(curEntry.GetEntry()->m_sourceId), DevKitOnly_StringIdToString(curEntry.GetEntry()->m_remapId), DevKitOnly_StringIdToString(curEntry.GetSet()->m_name));
	}
	MsgAnim("Num unused entries: %d\n", numunused);	
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimOverlayUseCheck::Matches(const OverlayState& state, const OverlayEntryInfo& entry)
{
	const bool isState = (entry.GetEntry()->m_flags & DC::kAnimOverlayFlagsIsState) != 0;
	return ((isState == state.IsState())
		&& (entry.GetEntry()->m_sourceId == state.GetId()));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlayUseCheck::AddOverlaySetEntries(const ListArray<const DC::AnimOverlaySet*>& overlays,
											   OverlayEntrySet& entrySet)
{
	for (U32 i = 0; i < overlays.Size(); ++i)
	{
		const DC::AnimOverlaySet* pSet = overlays[i];

		for (U32 j = 0; j < pSet->m_animOverlaySetArrayCount; ++j)
		{
			entrySet.Insert(OverlayEntryInfo(&pSet->m_animOverlaySetArray[j], pSet));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlayUseCheck::AddDynamicAnims(const DynamicAnimations& dynamicAnims, OverlayStateSet& overlaystateSet)
{
	for (I32 i = 0; i < dynamicAnims.List().Size(); ++i)
	{
		overlaystateSet.Insert(OverlayState(dynamicAnims.List()[i], false));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlayUseCheck::AddOverlayStateFromAnimActors(const DC::AnimActor* pAnimActor,
														OverlayStateSet& overlaystateSet)
{
	AnimActorStateIterator itState(pAnimActor);
	while (!itState.AtEnd())
	{
		const DC::AnimState* pState = itState.GetState();
		AddOverlayStatesFromState(pState, overlaystateSet);
		itState.Advance();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimOverlayUseCheck::AddOverlayStatesFromState(const DC::AnimState* pState, OverlayStateSet& overlaystateSet)
{
	overlaystateSet.Insert(OverlayState(pState->m_name.m_symbol, true));
	AddOverlayStatesFromTree(pState->m_tree, overlaystateSet);		
	overlaystateSet.Insert(OverlayState(pState->m_phaseAnimName, false));
}
