/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-defines.h"
#include "ndlib/script/script-manager.h"

/// --------------------------------------------------------------------------------------------------------------- ///
namespace DC
{
	struct AnimInfoCollection;
	struct AnimState;
	struct AnimTransition;

	typedef I32 AnimNodeChannel;
};

class AnimCmdList;
class AnimOverlaySnapshot;
class AnimSnapshotNodeBlend;
class AnimStateInstance;
class AnimTable;
struct AnimCmdGenInfo;
struct AnimCmdGenLayerContext;
struct FgAnimData;

/// --------------------------------------------------------------------------------------------------------------- ///
typedef void AnimNodeBlendCallBack_PreBlend(const AnimCmdGenInfo* pAnimCmdGenInfo,
											AnimCmdList* pAnimCmdList,
											const AnimSnapshotNodeBlend* pNode,
											I32F leftInstace,
											I32F rightInstance,
											I32F outputInstance,
											ndanim::BlendMode blendMode);
typedef void AnimNodeBlendCallBack_PostBlend(const AnimCmdGenInfo* pAnimCmdGenInfo,
											 AnimCmdList* pAnimCmdList,
											 const AnimSnapshotNodeBlend* pNode,
											 I32F leftInstace,
											 I32F rightInstance,
											 I32F outputInstance,
											 ndanim::BlendMode blendMode);
typedef void AnimStateLayerPostStateCallBack(const AnimCmdGenInfo* pAnimCmdGenInfo,
											 AnimCmdList* pAnimCmdList,
											 AnimStateInstance* pInstance,
											 SkeletonId skelId,
											 I32F outputInstance,
											 U32F trackIndex,
											 U32F instanceIndex);
typedef void AnimStateLayerBlendStatesCallBack(const AnimCmdGenLayerContext& context,
											   AnimCmdList* pAnimCmdList,
											   I32F leftInstace,
											   I32F rightInstance,
											   I32F outputInstance,
											   ndanim::BlendMode blendMode,
											   float blend);
typedef bool (*AnimStateLayerFilterAPRefCallBack)(const AnimStateInstance* pInstance);

/// --------------------------------------------------------------------------------------------------------------- ///
struct sAnimNodeBlendCallbacks
{
	AnimStateLayerPostStateCallBack* m_postState		= nullptr;
	AnimStateLayerBlendStatesCallBack* m_stateBlendFunc = nullptr;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmdGenInfo
{
	const AnimStateInstance* m_pInst;
	const AnimCmdGenLayerContext* m_pContext;
	const AnimTable* m_pAnimTable;
	const DC::AnimInfoCollection* m_pInfoCollection;
	float m_statePhase;
	bool m_allowTreePruning;
	sAnimNodeBlendCallbacks m_blendCallBacks;
	bool m_cameraCutThisFrame;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class IAnimTransitionSearch
{
public:
	virtual const DC::AnimState* Search(const DC::AnimState* pState,
										StringId64 destStateId,
										const DC::AnimInfoCollection* pInfoCollection) = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
/// BlendOverlay, which allows animator to customize blend time / curve between anim-state or anims.
/// --------------------------------------------------------------------------------------------------------------- ///
struct BlendOverlay
{
	ScriptPointer<DC::BlendOverlayMap> m_pDefaultOverlay;
	ScriptPointer<DC::BlendOverlayMap> m_pCustomOverlay;

	static const DC::BlendOverlayEntry* Lookup(const DC::BlendOverlayMap* pMap, const DC::BlendOverlayKey& key);

	// convenient func to lookup DC::BlendOverlayEntry*
	const DC::BlendOverlayEntry* Lookup(StringId64 srcId, StringId64 dstId) const;

};

/// --------------------------------------------------------------------------------------------------------------- ///
struct TransitionQueryInfo
{
	TransitionQueryInfo()
	: m_pInfoCollection(nullptr)
	, m_pAnimTable(nullptr)
	, m_pOverlaySnapshot(nullptr)
	, m_pStateBlendOverlay(nullptr)
	, m_phase(0.0f)
	, m_frame(0.0f)
	, m_updateRate(0.0f)
	, m_stateFade(0.0f)
	, m_stateId(INVALID_STRING_ID_64)
	, m_phaseAnimId(INVALID_STRING_ID_64)
	, m_phaseAnimLooping(false)
	, m_isTopIntance(false)
	, m_hasFreeInstance(true)
	{}
	const DC::AnimInfoCollection* m_pInfoCollection;
	const AnimTable* m_pAnimTable;
	const AnimOverlaySnapshot* m_pOverlaySnapshot;
	const BlendOverlay* m_pStateBlendOverlay;
	float m_phase;
	float m_frame;
	float m_updateRate;
	float m_stateFade;
	StringId64 m_stateId;
	StringId64 m_phaseAnimId;
	bool m_phaseAnimLooping;
	bool m_isTopIntance;
	bool m_hasFreeInstance;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class IAnimStateFilter
{
public:
	virtual ~IAnimStateFilter() {}
	virtual bool AnimStateValid(const DC::AnimState* pState) = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimStateUserIdFilter : public IAnimStateFilter
{
public:
	AnimStateUserIdFilter(StringId64 userId);
	virtual bool AnimStateValid(const DC::AnimState* pState) override;

private:
	StringId64 m_userId;
};

/// --------------------------------------------------------------------------------------------------------------- ///
// Anim State
extern const DC::AnimTransition* AnimStateGetActiveTransitionByName(const DC::AnimState* pState,
																	StringId64 transitionId,
																	const TransitionQueryInfo& info);
extern const DC::AnimTransition* AnimStateGetActiveTransitionByState(const DC::AnimState* pState,
																	 StringId64 stateId,
																	 const TransitionQueryInfo& info);
extern const DC::AnimTransition* AnimStateGetActiveTransitionByStateFilter(const DC::AnimState* pState,
																		   IAnimStateFilter* pFilter,
																		   const TransitionQueryInfo& info);
extern const DC::AnimTransition* AnimStateGetFirstTransition(const DC::AnimState* pState);
extern const DC::AnimTransition* AnimStateGetNextTransition(const DC::AnimState* pState, DC::AnimTransition* pTrans);
extern bool AnimStateTransitionExists(const DC::AnimState* pState,
									  StringId64 transitionId,
									  const TransitionQueryInfo& info);
extern bool AnimStateTransitionValid(const DC::AnimState* pState,
									 StringId64 transitionId,
									 const TransitionQueryInfo& info);
extern const DC::AnimTransition* AnimStateGetTransitionByFinalState(const DC::AnimState* pState,
																	StringId64 destStateId,
																	const DC::AnimInfoCollection* pInfoCollection,
																	IAnimTransitionSearch* pSearcher = nullptr);
extern bool CanPathfindThrough(const DC::AnimTransition* trans, StringId64 destId);
extern bool AnimStateHasTransitionId(const DC::AnimState* pState, StringId64 transitionId);

extern const DC::AnimTransition* AnimStateGetTransitionByName(const DC::AnimState* pState, StringId64 transitionId);

/// --------------------------------------------------------------------------------------------------------------- ///
// Anim Transition
extern bool AnimTransitionActive(const DC::AnimTransition* pTrans, const TransitionQueryInfo& info);

/// --------------------------------------------------------------------------------------------------------------- ///
extern void AnimChannelJointBlend(ndanim::JointParams* pDest,
								  const ndanim::JointParams& start,
								  const ndanim::JointParams& end,
								  float tt);
extern void AnimChannelJointBlendRadial(ndanim::JointParams* pDest,
										const ndanim::JointParams& start,
										const ndanim::JointParams& end,
										float tt);

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename T>
const T* AnimStateLookupStateInfo(const DC::AnimState* pState, StringId64 infoId)
{
	const DC::HashTable* pInfoTable = pState ? pState->m_stateInfoTable : nullptr;
	if (!pInfoTable)
	{
		return nullptr;
	}

	const T* pRet = ScriptManager::HashLookup<T>(pInfoTable, infoId);
	return pRet;
}
