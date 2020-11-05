/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/util/msg.h"

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-instance.h"
#include "ndlib/anim/anim-state.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimStateLayer;
struct FgAnimData;
class AnimCmdList;
class AnimOverlaySnapshot;
class AnimStateInstance;
class BoundFrame;
class EffectList;
class Locator;
struct AnimCmdGenLayerContext;
class AnimCopyRemapLayer;

namespace DC
{
	struct AnimActor;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimStateInstanceTrack
{
public:
	AnimStateInstanceTrack();

	void Allocate(U32F maxStateInstances, const AnimOverlaySnapshot* pOverlaySnapshot);
	void Init(const AnimOverlaySnapshot* pOverlaySnapshot);
	void Reset();
	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);
	void UpdateOverlaySnapshot(const AnimOverlaySnapshot* pSourceSnapshot);

	bool CanPushInstance() const;
	bool PushInstance(AnimStateInstance* pInstance);

	U32F GetMaxNumInstances() const { return m_maxNumInstances; }
	U32F GetNumInstances() const { return m_numInstances; }

	F32 MasterFade() const;
	F32 AnimFade() const;
	F32 MotionFade() const;

	void DebugPrint(MsgOutput output,
					bool additiveLayer,
					bool baseLayer,
					const FgAnimData* pAnimData,
					const AnimCopyRemapLayer* pRemapLayer = nullptr) const;

	void CreateAnimCmds(const AnimStateLayer* pLayer,
						const AnimCmdGenLayerContext& context,
						AnimCmdList* pAnimCmdList,
						U32F outputInstanceIndex,
						U32F trackIndex,
						F32 layerFadeOverride = -1) const;

	const AnimStateInstance* CurrentStateInstance() const;
	AnimStateInstance* CurrentStateInstance();
	const AnimStateInstance* OldestStateInstance() const;
	AnimStateInstance* OldestStateInstance();
	AnimStateInstance* GetInstance(U32F index);
	const AnimStateInstance* GetInstance(U32F index) const;

	AnimStateInstance* ReclaimInstance();

	const AnimStateInstance* FindInstanceByName(StringId64 stateName) const;
	const AnimStateInstance* FindInstanceByNameNewToOld(StringId64 stateName) const;
	const AnimStateInstance* FindInstanceById(AnimInstance::ID id) const;

	void DeleteNonContributingInstances(AnimStateLayer* pStateLayer);
	void ResetAnimStateChannelDeltas();
	bool UpdateInstanceFadeEffects(F32 deltaTime, bool freezeNextInstancePhase);
	void UpdateInstancePhases(F32 deltaTime, bool topLayer, EffectList* pTriggeredEffects);
	void UpdateEffectiveFade(float effectiveTrackFade);
	void RefreshAnimPointers();
	void DebugOnly_ForceUpdateInstanceSnapShots(const DC::AnimActor* pAnimActor, const FgAnimData* pAnimData);

	void UpdateAllApReferences(const BoundFrame& apReference, AnimStateLayerFilterAPRefCallBack filterCallback);
	bool UpdateAllApReferencesUntilFalse(const BoundFrame& apReference,
										 AnimStateLayerFilterAPRefCallBack filterCallback);
	void UpdateAllApReferencesTranslationOnly(Point_arg newApPos, AnimStateLayerFilterAPRefCallBack filterCallback);
	void TransformAllApReferences(const Locator& oldSpace, const Locator& newSpace);

	bool WalkInstancesNewToOld(PFnVisitAnimStateInstance pfnCallback, AnimStateLayer* pStateLayer, uintptr_t userData);
	bool WalkInstancesNewToOld(PFnVisitAnimStateInstanceConst pfnCallback,
							   const AnimStateLayer* pStateLayer,
							   uintptr_t userData) const;
	bool WalkInstancesOldToNew(PFnVisitAnimStateInstance pfnCallback, AnimStateLayer* pStateLayer, uintptr_t userData);
	bool WalkInstancesOldToNew(PFnVisitAnimStateInstanceConst pfnCallback,
							   const AnimStateLayer* pStateLayer,
							   uintptr_t userData) const;

	bool HasInstance(const AnimStateInstance* pInstance) const;

	const AnimOverlaySnapshot* GetOverlaySnapshot() const { return m_pOverlaySnapshot; }
	AnimOverlaySnapshot* GetOverlaySnapshot() { return m_pOverlaySnapshot; }

private:
	// qw0
	AnimStateInstance** m_ppInstanceList;
	AnimOverlaySnapshot* m_pOverlaySnapshot;
	U16 m_maxNumInstances;
	U16 m_numInstances;
	U8 m_pad[4];
};
