/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/util/error.h"
#include "corelib/util/msg.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/process/process.h"
#include "ndlib/scriptx/h/animation-script-types.h"

class AnimCmdGeneratorLayer;
class AnimCmdList;
class AnimDummyInstance;
class AnimLayer;
class AnimOverlaySnapshot;
class AnimOverlays;
class AnimPoseLayer;
class AnimSimpleLayer;
class AnimSnapshotLayer;
class AnimStateLayer;
class AnimCopyRemapLayer;
class ArtItemAnim;
class ArtItemSkeleton;
class DualSnapshotNode;
class IAnimCmdGenerator;
class JointCache;
class Locator;
struct AnimCmdGenContext;
struct FgAnimData;

//#define DEPRECATE __attribute__((deprecated))
#define DEPRECATE

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimStateLayerParams
{
	static CONST_EXPR U32 kDefaultPriority = 0x7fffffff;

	ndanim::BlendMode m_blendMode = ndanim::kBlendSlerp;
	U32 m_priority		   = kDefaultPriority;
	U32 m_numTracksInLayer = 1;
	U32 m_numInstancesInLayer = 1;

	// deprecated!
	U32 m_maxNumSnapshotNodes	= 0;
	U32 m_snapshotHeapSizeBytes = 0;
	U32 m_snapshotHeapId		= 0;

	const StringId64* m_pChannelIds = nullptr;
	U32 m_numChannelIds	 = 0;
	bool m_cacheTopInfo	 = false;
	bool m_cacheOverlays = false;
	FadeToStateParams::NewInstanceBehavior m_newInstanceBehavior = FadeToStateParams::kUsePreviousTrack;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class ALIGNED(16) AnimControl
{
public:
	friend class NdActorViewerObject;

	typedef U32 LayerCreationOptions;
	enum ELayerCreationOptions
	{
		kLayerCreateOptionNoLegIk = 1u << 0,
		kLayerCreateOptionUnused1 = 1u << 1,
		kLayerCreateOptionUnused2 = 1u << 2,
		kLayerCreateOptionAll     = 0xFFFFu // 16 bits are currently available for creation options
	};

	AnimControl();
	Err Init(FgAnimData* pAnimData,
			 U32F maxTriggeredEffects,
			 U32F maxLayers,
			 Process* pOwner = nullptr);
	Err SetupAnimActor(StringId64 animActorName);
	Err ConfigureStartState(StringId64 startStateOverride, bool useRandomStartPhase);
	void Shutdown();

	FgAnimData& GetAnimData()				{ return *m_pAnimData; }
	const FgAnimData& GetAnimData() const	{ return *m_pAnimData; }
	const ndanim::JointHierarchy* GetSkeleton() const;
	const ArtItemSkeleton* GetArtItemSkel() const;
	JointCache* GetJointCache();
	const JointCache* GetJointCache() const;

	void SetLayerCreationOptions(LayerCreationOptions optBits) { m_layerCreationOptionBits = (U16)(optBits & kLayerCreateOptionAll); }
	LayerCreationOptions GetLayerCreationOptions() const { return (LayerCreationOptions)m_layerCreationOptionBits; }

	void EnableAsyncUpdate(U32 updateBitMask);
	void DisableAsyncUpdate(U32 updateBitMask);
	bool IsAsyncUpdateEnabled(U32 updateBitMask);

	void AllocateSimpleLayer(StringId64 nameId, ndanim::BlendMode blendMode, U32 priority, U32F numInstancesInLayer);
	void AllocateStateLayer(StringId64 nameId, const AnimStateLayerParams& params);
	void AllocatePoseLayer(StringId64 nameId, ndanim::BlendMode blendMode, U32 priority);
	void AllocateCmdGeneratorLayer(StringId64 nameId, ndanim::BlendMode blendMode, U32 priority);
	void AllocateSnapshotLayer(StringId64 nameId, ndanim::BlendMode blendMode, U32 priority);
	void AllocateCopyRemapLayer(StringId64 nameId, ndanim::BlendMode blendMode, U32 priority);

	// Deprecated Interface
	void AllocateSimpleLayer(U32F numInstancesInLayer) DEPRECATE;
	void AllocatePoseLayer() DEPRECATE;
	void AllocateCmdGeneratorLayer() DEPRECATE;
	void AllocateSnapshotLayer() DEPRECATE;

	void AllocateOverlays(U32 instanceSeed, StringId64 priorityNameId, bool uniDirectional = false);

	void NotifyAnimTableUpdated();


	CachedAnimLookupRaw LookupAnimCached(const CachedAnimLookupRaw& lookup) const;
	CachedAnimLookup LookupAnimCached(const CachedAnimLookup& lookup) const;
	StringId64 LookupAnimId(StringId64 animNameId) const;
	ArtItemAnimHandle LookupAnim_NoTranslate(StringId64 animNameId) const;
	ArtItemAnimHandle LookupAnim(StringId64 animNameId) const;
	ArtItemAnimHandle LookupAnimNoOverlays(StringId64 animNameId) const; //Dont tranform with overlays, but still use retargetting

	AnimOverlaySnapshot::Result DevKitOnly_LookupAnimId(StringId64 animNameId) const;
	const char* DevKitOnly_LookupAnimName(StringId64 animNameId) const;

	AnimTable& GetAnimTable();
	const AnimTable& GetAnimTable() const;

	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	AnimSimpleLayer* CreateSimpleLayer(StringId64 layerName);
	AnimStateLayer* CreateStateLayer(StringId64 layerName, StringId64 startStateId);
	AnimStateLayer* CreateStateLayer(StringId64 layerName);
	AnimPoseLayer* CreatePoseLayer(StringId64 layerName, const ndanim::PoseNode* pPoseNode);
	AnimPoseLayer* CreatePoseLayer(StringId64 layerName, const DualSnapshotNode* pSnapshotNode);
	AnimCmdGeneratorLayer* CreateCmdGeneratorLayer(StringId64 layerName, IAnimCmdGenerator* pAnimCmdGenerator);
	AnimSnapshotLayer* CreateSnapshotLayer(StringId64 layerName, DualSnapshotNode* pSnapshotNode);
	AnimCopyRemapLayer* CreateCopyRemapLayer(StringId64 layerName,
											 const NdGameObject* pOwner,
											 const NdGameObject* pTargetChar,
											 StringId64 targetLayer,
											 StringId64 remapList);

	// Deprecated Interface
	AnimSimpleLayer* CreateSimpleLayer(StringId64 layerName, ndanim::BlendMode blendMode, U32F priority) DEPRECATE;
	AnimStateLayer* CreateStateLayer(StringId64 layerName,
									 StringId64 startStateId,
									 ndanim::BlendMode blendMode,
									 U32F priority) DEPRECATE;
	AnimStateLayer* CreateStateLayer(StringId64 layerName, ndanim::BlendMode blendMode, U32F priority) DEPRECATE;
	AnimPoseLayer* CreatePoseLayer(StringId64 layerName,
								   ndanim::BlendMode blendMode,
								   U32F priority,
								   const ndanim::PoseNode* pPoseNode) DEPRECATE;
	AnimCmdGeneratorLayer* CreateCmdGeneratorLayer(StringId64 layerName,
												   ndanim::BlendMode blendMode,
												   U32F priority,
												   IAnimCmdGenerator* pAnimCmdGenerator) DEPRECATE;

	void DebugPrint(const Locator& currentAlign,
					StringId64 aLayersToSuppress[] = nullptr,
					MsgOutput output = kMsgCon) const; // prints all layers if aLayersToSuppress == nullptr

	U32F GetNumFadesInProgress() const;

	DC::AnimInfoCollection* GetInfoCollection() const;
	DC::AnimActorInfo* GetInfo() const;
	DC::AnimInstanceInfo* GetInstanceInfo() const;
	DC::AnimTopInfo* GetTopInfo() const;
	const DC::AnimActor* GetActor() const;

	template<typename T> T* Info()			{ ANIM_ASSERT(GetInfo());			return static_cast<T*>(GetInfo());			}
	template<typename T> T* InstanceInfo()	{ ANIM_ASSERT(GetInstanceInfo());	return static_cast<T*>(GetInstanceInfo());	}
	template<typename T> T* TopInfo()		{ ANIM_ASSERT(GetTopInfo());		return static_cast<T*>(GetTopInfo());		}

	template<typename T> const T* Info() const			{ ANIM_ASSERT(GetInfo());			return static_cast<const T*>(GetInfo());			}
	template<typename T> const T* InstanceInfo() const	{ ANIM_ASSERT(GetInstanceInfo());	return static_cast<const T*>(GetInstanceInfo());	}
	template<typename T> const T* TopInfo() const		{ ANIM_ASSERT(GetTopInfo());		return static_cast<const T*>(GetTopInfo());			}

	U32F GetNumLayers() const { return m_numLayers; }
	U32F GetMaxNumLayers() const { return m_maxLayers; }

	StringId64 GetLayerNameByIndex(int index) const;

	AnimLayer* GetLayerByIndex(int index);
	AnimLayer* GetLayerById(StringId64 name);
	AnimSimpleLayer* GetSimpleLayerById(StringId64 name);
	AnimSimpleLayer* GetSimpleLayerByIndex(I32 index);
	AnimStateLayer* GetStateLayerById(StringId64 name);
	AnimStateLayer* GetStateLayerByIndex(I32 index);
	AnimPoseLayer* GetPoseLayerById(StringId64 name);
	AnimSnapshotLayer* GetSnapshotLayerById(StringId64 name);
	AnimCmdGeneratorLayer* GetCmdGeneratorLayerById(StringId64 name);
	AnimStateLayer* GetBaseStateLayer();
	const AnimLayer* GetLayerByIndex(int index) const;
	const AnimLayer* GetLayerById(StringId64 name) const;
	const AnimSimpleLayer* GetSimpleLayerById(StringId64 name) const;
	const AnimStateLayer* GetStateLayerById(StringId64 name) const;
	const AnimStateLayer* GetStateLayerByIndex(I32 index) const;
	const AnimPoseLayer* GetPoseLayerById(StringId64 name) const;
	const AnimSnapshotLayer* GetSnapshotLayerById(StringId64 name) const;
	const AnimCmdGeneratorLayer* GetCmdGeneratorLayerById(StringId64 name) const;
	const AnimStateLayer* GetBaseStateLayer() const;

	AnimSimpleLayer* FindSimpleLayerByAnim(StringId64 anim);
	const AnimSimpleLayer* FindSimpleLayerByAnim(StringId64 anim) const;

	void DestroyLayerById(StringId64 name);

	// dummy animation instance, for keeping track of elapsed animation time when an animation within a cinematic has been disabled / is not present
	AnimDummyInstance* CreateDummyInstance(const StringId64 animId,
	                                       F32 num30HzFrameIntervals,
	                                       F32 startPhase,
	                                       F32 playbackRate,
	                                       bool looping = false,
	                                       ndanim::SharedTimeIndex* pSharedTime = nullptr);
	AnimDummyInstance* GetDummyInstance() const;
	void FadeOutDummyInstanceAndDestroy(float fadeOutSec);
	void DestroyDummyInstance();

	AnimOverlays* GetAnimOverlays() { return m_pAnimOverlays; }
	const AnimOverlays* GetAnimOverlays() const { return m_pAnimOverlays; }
	AnimOverlaySnapshot* GetAnimOverlaySnapshot();
	const AnimOverlaySnapshot* GetAnimOverlaySnapshot() const;

	// Layer trampoline functions
	void SetLayerPriority(StringId64 layerName, U32F priority);
	U32F GetLayerPriority(StringId64 layerName);
	bool FadeLayer(StringId64 layerName, float desiredFade, float fadeTime, DC::AnimCurveType type = DC::kAnimCurveTypeLinear);
	bool FadeOutLayerAndDestroy(StringId64 layerName, float blendTime, DC::AnimCurveType type = DC::kAnimCurveTypeLinear);

	// Convenience joint info functions
	U32 GetJointCount() const;
	const char* GetJointName(U32 iJoint) const;
	StringId64 GetJointSid(U32 iJoint) const;

	static U32F GetBaseLayerPriority() { return 14; }

	// Only the animation manager is allowed to call this function.
	void BeginStep(float deltaTime);
	void BeginStepInternal(float deltaTime);
	void FinishStep(float deltaTime);

	// Functions interpreting the layers and states to generate intermediate animation commands.
	void CreateAnimCmds(const AnimCmdGenContext& context, AnimCmdList* pAnimCmdList) const;

	// Effect management
	const EffectList* GetTriggeredEffects() const;

	bool SetCustomStateBlendOverlay(StringId64 blendOverlay);
	void SetDefaultStateBlendOverlay(StringId64 blendOverlay);
	void SetDefaultAnimBlendOverlay(StringId64 blendOverlay);

	const BlendOverlay* GetStateBlendOverlay() const { return &m_stateBlendOverlay; }
	const BlendOverlay* GetAnimBlendOverlay() const { return &m_animBlendOverlay; }

	// convenient func to lookup DC::BlendOverlayEntry*
	const DC::BlendOverlayEntry* LookupStateBlendOverlay(StringId64 srcId, StringId64 dstId) const;
	void ReloadScriptData();
	void DebugOnly_ForceUpdateOverlaySnapshot(AnimOverlaySnapshot* pNewSnapshot);

	bool IsAnimStepJobFinished() const { return m_animStepJobFinished; }
	void FinishAnimStepJob() { m_animStepJobFinished = true; }
	void SetAnimStepJobStarted() { m_animStepJobFinished = false;}

	bool IsRandomizationDisabled() const { return m_disableRandomization; }
	void SetRandomizationDisabled(bool d) { m_disableRandomization = d;  }

	void SetInitializeFirstInstance(bool init) { m_alwaysInitInstanceZero = init; }

	void SetCameraCutsDisabled(bool disabled) { m_cameraCutsDisabled = disabled; } // used by cinematic system to disable camera cut detection for all objects in a "slaved" cinematic
	bool AreCameraCutsDisabled() const { return m_cameraCutsDisabled; }

	void ChangeAnimData(FgAnimData *pAnimData);

private:
	struct LayerEntry
	{
		StringId64 m_nameId;
		AnimLayer* m_pLayer;
		U32 m_priority;
		bool m_created;
		bool m_fixedName;
		AnimLayerType m_type;
	};

	ArtItemAnimHandle DevKitOnly_LookupAnimByIndex(int index) const;
	LayerEntry* FindLayerEntryById(StringId64 name);
	AnimLayer* CreateLayerHelper(AnimLayerType type, StringId64 layerName, U32F priority);
	AnimLayer* CreateLayerHelper(StringId64 layerName);
	void AllocateLayerHelper(AnimLayer* pLayer, AnimLayerType type, StringId64 nameId, U32 priority);
	const AnimLayer* FindLayerById(StringId64 name) const;
	void SortLayers(LayerEntry* pEntries);

	EffectList m_triggeredEffects;					// relo
	FgAnimData* m_pAnimData;						// non-relo

	U32 m_numLayers;						// Number of actually allocated layers
	U32 m_maxLayers;						// Max layers to allocate entries for
	LayerEntry* m_layerEntries;				// relo layer pointers
public:	// yeah I want to be able to access the owner in debug only code
	ProcessHandle m_hOwner;					// for debugging only (*really* useful so please don't remove unless absolutely necessary!)
private:
	U32 m_layerTypesContained;

	// AnimActor/AnimStateLayer specific variables
	StringId64 m_animActorName;
	const DC::AnimActor* m_pAnimActor;
	DC::AnimInfoCollection* m_pInfoCollection; // Needs relocation - Instance Data
	AnimOverlays* m_pAnimOverlays;

	BlendOverlay m_stateBlendOverlay; // anim-state to anim-state blend-overlay
	BlendOverlay m_animBlendOverlay; // anim to anim blend-overlay

	AnimDummyInstance* m_pDummyInstance; // hackette for cinematics (the "right" way to do this would be to integrate the dummy instance into the AnimLayer system for real, but that's overkill)

	AnimTable m_animTable;

	U16 m_layerCreationOptionBits;
	bool m_animStepJobFinished;

	bool m_disableRandomization;
	bool m_alwaysInitInstanceZero;
	bool m_cameraCutsDisabled;
};
