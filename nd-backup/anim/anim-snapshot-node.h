/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-data-eval.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-node-library.h" // IWYU pragma: keep
#include "ndlib/anim/snapshot-node-heap.h"
#include "ndlib/memory/relocatable-heap-rec.h"
#include "ndlib/util/type-factory.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class AnimSnapshotNodeBlend;
class AnimStateSnapshot;
class ArtItemSkeleton;
class EffectList;
struct AnimCameraCutInfo;
struct AnimCmdGenInfo;
struct AnimNodeDebugPrintData;
struct SnapshotAnimNodeTreeParams;
struct SnapshotAnimNodeTreeResults;
struct SnapshotEvaluateParams;
class AnimInstance;

/// --------------------------------------------------------------------------------------------------------------- ///
namespace DC
{
	struct AnimActorInfo;
	struct AnimInfoCollection;
	struct AnimInstanceInfo;
	struct AnimNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct EffectUpdateStruct
{
	const DC::AnimActorInfo* m_info;
	const DC::AnimInstanceInfo* m_instanceInfo;
	float m_oldPhase;
	float m_newPhase;
	float m_stateEffectiveFade;
	float m_minBlend;
	bool m_isFlipped;
	bool m_isTopState;
	bool m_isReversed;
	StringId64 m_stateId;
	bool m_isMotionMatching;
	float m_animBlend;
	EffectList* m_pTriggeredEffects;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSnapshotNode : public Relocatable
{
public:
	explicit AnimSnapshotNode(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex);
	virtual ~AnimSnapshotNode() override {}

	//
	// Relocatable interface
	//
	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	const char* GetName() const override;
	RelocatableHeapRecord* GetHeapRecord() const override { return m_pHeapRec; }

	//
	// Construction, update, etc
	//
	virtual void SnapshotNode(AnimStateSnapshot* pSnapshot,
							  const DC::AnimNode* pDcAnimNode,
							  const SnapshotAnimNodeTreeParams& params,
							  SnapshotAnimNodeTreeResults& results)
	{
		ANIM_ASSERTF(false, ("SnapshotNode() for '%s' is unimplemented", DevKitOnly_StringIdToString(m_typeId)));
	}

	virtual bool RefreshAnims(AnimStateSnapshot* pSnapshot) { return false; }
	virtual bool RefreshPhasesAndBlends(AnimStateSnapshot* pSnapshot,
										float statePhase,
										bool topTrackInstance,
										const DC::AnimInfoCollection* pInfoCollection) { return false; }
	virtual U8 RefreshBreadth(AnimStateSnapshot* pSnapshot) { return 1; }
	virtual void StepNode(AnimStateSnapshot* pSnapshot, float deltaTime, const DC::AnimInfoCollection* pInfoCollection) {}
	virtual void GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
									  AnimCmdList* pAnimCmdList,
									  I32F outputInstance,
									  const AnimCmdGenInfo* pCmdGenInfo) const {}
	virtual void ReleaseNodeRecursive(SnapshotNodeHeap* pHeap) const;

	virtual void OnAddedToBlendNode(const SnapshotAnimNodeTreeParams& params,
									AnimStateSnapshot* pSnapshot,
									AnimSnapshotNodeBlend* pBlendNode,
									bool leftNode)
	{
	}

	virtual void GenerateAnimCommands_CustomBlend(const AnimSnapshotNodeBlend* pParentBlendNode,
												  const AnimSnapshotNode* pChildNode,
												  float nodeBlendToUse,
												  const AnimStateSnapshot* pSnapshot,
												  AnimCmdList* pAnimCmdList,
												  I32F outputInstance,
												  const AnimCmdGenInfo* pCmdGenInfo) const
	{
		ANIM_ASSERTF(false, ("Unimplemented custom blend op node '%s'", DevKitOnly_StringIdToString(m_typeId)));
	}

	virtual void GenerateAnimCommands_PreBlend(const AnimStateSnapshot* pSnapshot,
											   AnimCmdList* pAnimCmdList,
											   const AnimCmdGenInfo* pCmdGenInfo,
											   const AnimSnapshotNodeBlend* pBlendNode,
											   bool leftNode,
											   I32F leftInstance,
											   I32F rightInstance,
											   I32F outputInstance) const
	{
	}

	virtual void GenerateAnimCommands_PostBlend(const AnimStateSnapshot* pSnapshot,
												AnimCmdList* pAnimCmdList,
												const AnimCmdGenInfo* pCmdGenInfo,
												const AnimSnapshotNodeBlend* pBlendNode,
												bool leftNode,
												I32F leftInstance,
												I32F rightInstance,
												I32F outputInstance) const
	{
	}

	void SetHeapRecord(RelocatableHeapRecord* pRec) { m_pHeapRec = pRec; }
	virtual SnapshotNodeHeap::Index Clone(SnapshotNodeHeap* pDestHeap, const SnapshotNodeHeap* pSrcHeap) const;

	//
	// Various queries
	//
	virtual ndanim::ValidBits GetValidBits(const ArtItemSkeleton* pSkel,
										   const AnimStateSnapshot* pSnapshot,
										   U32 iGroup) const
	{
		return ndanim::ValidBits(0, 0);
	}

	virtual bool IsAdditive(const AnimStateSnapshot* pSnapshot) const { return false; }
	virtual bool IsFlipped() const { return false; }
	virtual bool ShouldHandleFlipInBlend() const { return false; }
	virtual bool AllowPruning(const AnimStateSnapshot* pSnapshot) const { return false; }
	virtual bool HasErrors(const AnimStateSnapshot* pSnapshot) const { return false; }

	virtual bool HasLoopingAnimation(const AnimStateSnapshot* pSnapshot) const { return false; }
	virtual void GetTriggeredEffects(const AnimStateSnapshot* pSnapshot,
									 EffectUpdateStruct& effectParams,
									 float nodeBlend,
									 const AnimInstance* pInstance) const {}

	virtual bool EvaluateFloatChannel(const AnimStateSnapshot* pSnapshot,
									  float* pOutChannelFloat,
									  const SnapshotEvaluateParams& evaluateParams) const
	{
		return false;
	}

	virtual bool EvaluateChannel(const AnimStateSnapshot* pSnapshot,
								 ndanim::JointParams* pOutChannelJoint,
								 const SnapshotEvaluateParams& evaluateParams) const
	{
		return false;
	}

	virtual bool EvaluateChannelDelta(const AnimStateSnapshot* pSnapshot,
									  ndanim::JointParams* pOutChannelJoint,
									  const SnapshotEvaluateParams& evaluateParams) const
	{
		return false;
	}

	virtual IAnimDataEval::IAnimData* EvaluateNode(IAnimDataEval* pEval, const AnimStateSnapshot* pSnapshot) const;

	virtual void GetHeapUsage(const SnapshotNodeHeap* pSrcHeap, U32& outMem, U32& outNumNodes) const;

	//
	// Debug Nonsense
	//
	virtual void DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const {}
	virtual void DebugSubmitAnimPlayCount(const AnimStateSnapshot* pSnapshot) const {}

	//
	// RTTI
	//
	const StringId64 GetDcType() const { return m_dcType; }
	const TypeFactory::Record& GetType() const { return *m_pType; }
	StringId64 GetTypeId() const { return m_typeId; }
	bool IsType(StringId64 sid) const { return sid == m_typeId; }
	bool IsKindOf(const TypeFactory::Record& parentType) const { return m_pType->IsKindOf(parentType); }
	bool IsKindOf(StringId64 typeId) const { return m_pType->IsKindOf(typeId); } // slower version

	virtual const AnimSnapshotNode* FindFirstNodeOfKind(const AnimStateSnapshot* pSnapshot, StringId64 typeId) const;

	void VisitNodesOfKind(AnimStateSnapshot* pSnapshot,
						  StringId64 typeId,
						  SnapshotVisitNodeFunc visitFunc,
						  uintptr_t userData);

	virtual bool VisitNodesOfKindInternal(AnimStateSnapshot* pSnapshot,
										  StringId64 typeId,
										  SnapshotVisitNodeFunc visitFunc,
										  AnimSnapshotNodeBlend* pParentBlendNode,
										  float combinedBlend,
										  uintptr_t userData);

	void VisitNodesOfKind(const AnimStateSnapshot* pSnapshot,
						  StringId64 typeId,
						  SnapshotConstVisitNodeFunc visitFunc,
						  uintptr_t userData) const;

	virtual bool VisitNodesOfKindInternal(const AnimStateSnapshot* pSnapshot,
										  StringId64 typeId,
										  SnapshotConstVisitNodeFunc visitFunc,
										  const AnimSnapshotNodeBlend* pParentBlendNode,
										  float combinedBlend,
										  uintptr_t userData) const;

	struct AnimIdCollection
	{
		AnimIdCollection() : m_animCount(0) {}

		static const U32 kMaxAnimIds = 128;

		StringId64 m_animArray[kMaxAnimIds];
		U32 m_animCount;
	};

	void CollectContributingAnimIds(const AnimStateSnapshot* pSnapshot,
									float blend,
									AnimIdCollection* pCollection) const;
	virtual void CollectContributingAnims(const AnimStateSnapshot* pSnapshot,
										  float blend,
										  AnimCollection* pCollection) const {}

	void ForAllAnimations(const AnimStateSnapshot* pSnapshot, AnimationVisitFunc visitFunc, uintptr_t userData) const;
	virtual void ForAllAnimationsInternal(const AnimStateSnapshot* pSnapshot,
										  AnimationVisitFunc visitFunc,
										  float combinedBlend,
										  uintptr_t userData) const;

private:
	StringId64 m_typeId;
	StringId64 m_dcType;
	const TypeFactory::Record* m_pType;
	SnapshotNodeHeap::Index m_nodeIndex;
	RelocatableHeapRecord* m_pHeapRec;
};

/// --------------------------------------------------------------------------------------------------------------- ///
// Registers a class.
#define ANIM_NODE_REGISTER_WITH_SNAPSHOT_FUNC(T, P, DCT, SF)                                                           \
	void* T##_CreateFunc(void* pParams, void* pMem)                                                                    \
	{                                                                                                                  \
		if (pMem)                                                                                                      \
			return NDI_NEW(pMem) T(SID(#T), DCT, *(SnapshotNodeHeap::Index*)pParams);                                  \
		else                                                                                                           \
			return NDI_NEW T(SID(#T), DCT, *(SnapshotNodeHeap::Index*)pParams);                                        \
	}                                                                                                                  \
	TypeFactory::Record g_type_##T(#T, SID(#T), &T##_CreateFunc, sizeof(T), SID(#P));                                  \
	AnimNodeLibrary::Record g_animNodeRec_##T(SID(#T), DCT, SF);

/// --------------------------------------------------------------------------------------------------------------- ///
//#define ANIM_NODE_REGISTER_WITH_SNAPSHOT_FUNC_MULTIPARENT(T, P, P2, DCT, SF)                                           \
//	void* T##_CreateFunc(void* pParams, void* pMem)                                                                    \
//	{                                                                                                                  \
//		if (pMem)                                                                                                      \
//			return NDI_NEW(pMem) T(SID(#T), DCT, *(SnapshotNodeHeap::Index*)pParams);                                  \
//		else                                                                                                           \
//			return NDI_NEW T(SID(#T), DCT, *(SnapshotNodeHeap::Index*)pParams);                                        \
//	}                                                                                                                  \
//	TypeFactory::Record g_type_##T(#T, SID(#T), &T##_CreateFunc, sizeof(T), SID(#P), SID(#P2));                        \
//	AnimNodeLibrary::Record g_animNodeRec_##T(SID(#T), DCT, SF);

#define ANIM_NODE_REGISTER(T, P, DCT) ANIM_NODE_REGISTER_WITH_SNAPSHOT_FUNC(T, P, DCT, nullptr)

/// --------------------------------------------------------------------------------------------------------------- ///
// Registers a base class.
#define ANIM_NODE_REGISTER_BASE(T, DCT)                                                                                \
	void* T##_CreateFunc(void* pParams, void* pMem)                                                                    \
	{                                                                                                                  \
		if (pMem)                                                                                                      \
			return NDI_NEW(pMem) T(SID(#T), DCT, *(SnapshotNodeHeap::Index*)pParams);                                  \
		else                                                                                                           \
			return NDI_NEW T(SID(#T), DCT, *(SnapshotNodeHeap::Index*)pParams);                                        \
	}                                                                                                                  \
	TypeFactory::Record g_type_##T(#T, SID(#T), &T##_CreateFunc, sizeof(T), INVALID_STRING_ID_64);                     \
	AnimNodeLibrary::Record g_animNodeRec_##T(SID(#T), DCT, nullptr);

/// --------------------------------------------------------------------------------------------------------------- ///
// Registers an abstract class.
#define ANIM_NODE_REGISTER_ABSTRACT(T, P) TYPE_FACTORY_REGISTER_ABSTRACT(T, P)

/// --------------------------------------------------------------------------------------------------------------- ///
// Registers an abstract base class.
#define ANIM_NODE_REGISTER_ABSTRACT_BASE(T) TYPE_FACTORY_REGISTER_ABSTRACT_BASE(T)

/// --------------------------------------------------------------------------------------------------------------- ///
// This macro externs the 'g_type_ ## ClassName' global for class ClassName, so that it can be passed
// to the fast version of IsKindOf().  Use of this macro is OPTIONAL - you need only use it for
// classes that are frequently involved in IsKindOf() checks.
#define ANIM_NODE_DECLARE(T) TYPE_FACTORY_DECLARE(T)

/// --------------------------------------------------------------------------------------------------------------- ///
// This macro declares the FromAnimNode() function pair.
#define FROM_ANIM_NODE_DECLARE(T)                                                                                      \
	static T* FromAnimNode(AnimSnapshotNode* pNode);                                                                   \
	static const T* FromAnimNode(const AnimSnapshotNode* pNode);                                                       \
	static StringId64 GetStaticTypeId() { return SID(#T); }

/// --------------------------------------------------------------------------------------------------------------- ///
// And this macro defines the FromAnimNode() function pair.
#define FROM_ANIM_NODE_DEFINE(T)                                                                                       \
	T* T::FromAnimNode(AnimSnapshotNode* pNode)                                                                        \
	{                                                                                                                  \
		T* pTypedNode = nullptr;                                                                                          \
		if (pNode && pNode->IsKindOf(g_type_##T))                                                                      \
		{                                                                                                              \
			pTypedNode = reinterpret_cast<T*>(pNode);                                                                  \
		}                                                                                                              \
		return pTypedNode;                                                                                             \
	}                                                                                                                  \
	const T* T::FromAnimNode(const AnimSnapshotNode* pNode)                                                            \
	{                                                                                                                  \
		const T* pTypedNode = nullptr;                                                                                    \
		if (pNode && pNode->IsKindOf(g_type_##T))                                                                      \
		{                                                                                                              \
			pTypedNode = reinterpret_cast<const T*>(pNode);                                                            \
		}                                                                                                              \
		return pTypedNode;                                                                                             \
	}

ANIM_NODE_DECLARE(AnimSnapshotNode);
