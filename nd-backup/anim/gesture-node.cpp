/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/gesture-node.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-copy-remap-layer.h"
#include "ndlib/anim/anim-node-hand-fix-ik.h"
#include "ndlib/anim/anim-node-ik.h"
#include "ndlib/anim/anim-node-joint-limits.h"
#include "ndlib/anim/anim-node-leg-fix-ik.h"
#include "ndlib/anim/anim-node-library.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-plugin-context.h"
#include "ndlib/anim/anim-snapshot-node-animation.h"
#include "ndlib/anim/anim-snapshot-node-blend.h"
#include "ndlib/anim/anim-snapshot-node-empty-pose.h"
#include "ndlib/anim/anim-stat.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/anim/anim-state.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/anim/feather-blend-table.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/ik/two-bone-ik.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/retarget-util.h"
#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/frame-params.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/netbridge/mail.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/render/display.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/script/script-manager.h"

#include "gamelib/anim/blinking.h"
#include "gamelib/anim/gesture-controller.h"
#include "gamelib/anim/gesture-target-manager.h"
#include "gamelib/anim/nd-gesture-util.h"
#include "gamelib/audio/conversation.h"
#include "gamelib/facts/fact-manager.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"
#include "gamelib/script/nd-script-arg-iterator.h"
#include "gamelib/state-script/ss-animate.h"
#include "gamelib/gameplay/character.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static NdAtomic32 s_gestureNodeUniqueId(0);

/// --------------------------------------------------------------------------------------------------------------- ///
class GestureNodeDebugDrawManager
{
public:
	// I drew a dir mesh this frame and this is where it is on screen.
	void RegisterDirMesh(I32 nodeUniqueId,
						 StringId64 gestureId,
						 const NdGameObject* pOwner,
						 const Transform& gestureRangeToScreen)
	{
		STRIP_IN_FINAL_BUILD;

		AtomicLockJanitor jj(&m_lock, FILE_LINE_FUNC);

		for (U32 i = 0; i < ARRAY_COUNT(m_meshes); ++i)
		{
			DirMesh& mesh = m_meshes[i];
			if (!mesh.IsCurrent())
			{
				mesh = DirMesh();
				mesh.m_owner = pOwner;
				mesh.m_gameFrameNumber = EngineComponents::GetNdFrameState()->m_gameFrameNumber;
				mesh.m_nodeUniqueId = nodeUniqueId;
				mesh.m_gestureId = gestureId;
				mesh.m_gestureRangeToScreen = gestureRangeToScreen;

				return;
			}
		}
	}

	struct DirMesh
	{
		NdGameObjectHandle m_owner = NdGameObjectHandle();
		I64 m_gameFrameNumber = -1;

		I32 m_nodeUniqueId;
		StringId64 m_gestureId;
		Transform m_gestureRangeToScreen;

		bool IsValid() const
		{
			return m_owner.HandleValid() && m_gameFrameNumber >= 0;
		}

		bool IsCurrent() const
		{
			if (!IsValid())
				return false;

			return (EngineComponents::GetNdFrameState()->m_gameFrameNumber - 1) <= m_gameFrameNumber;
		}
	};

	const DirMesh* GetDirMesh(I32 nodeUniqueId)
	{
		STRIP_IN_FINAL_BUILD_VALUE(nullptr);

		AtomicLockJanitor jj(&m_lock, FILE_LINE_FUNC);

		for (U32 i = 0; i < ARRAY_COUNT(m_meshes); ++i)
		{
			DirMesh& mesh = m_meshes[i];
			if (mesh.IsCurrent() && mesh.m_nodeUniqueId == nodeUniqueId)
			{
				return &mesh;
			}
		}

		return nullptr;
	}

	bool DidAlreadyDraw(const NdGameObject* pOwner, StringId64 gestureId)
	{
		STRIP_IN_FINAL_BUILD_VALUE(false);

		AtomicLockJanitor jj(&m_lock, FILE_LINE_FUNC);

		for (U32 i = 0; i < ARRAY_COUNT(m_meshes); ++i)
		{
			const DirMesh& mesh = m_meshes[i];
			if (mesh.IsValid() && mesh.m_gameFrameNumber == EngineComponents::GetNdFrameState()->m_gameFrameNumber
				&& mesh.m_owner.ToProcess() == pOwner
				&& mesh.m_gestureId == gestureId)
			{
				return true;
			}
		}

		return false;
	}

private:
	NdAtomicLock m_lock;

	DirMesh m_meshes[6];
};

/// --------------------------------------------------------------------------------------------------------------- ///
GestureNodeDebugDrawManager g_gestureNodeDebugDrawManager;

/// --------------------------------------------------------------------------------------------------------------- ///
struct GestureAnimPluginData
{
	IGestureNode* m_pSourceNode = nullptr;
	I32F m_sourceInstance = -1;
};

/// --------------------------------------------------------------------------------------------------------------- ///
ANIM_NODE_REGISTER_ABSTRACT(IGestureNode, AnimSnapshotNode);

FROM_ANIM_NODE_DEFINE(IGestureNode);

/// --------------------------------------------------------------------------------------------------------------- ///
static const IGestureNode* FindGestureNodeToInheritFrom(const GestureCache::CacheKey& newCacheKey,
														const BoundFrame& ownerLoc,
														bool requireExact,
														const SnapshotAnimNodeTreeParams& params);

/// --------------------------------------------------------------------------------------------------------------- ///
static bool NearlyEqual(const float a, const float b)
{
	const float diff = a - b;

	return -0.0001f <= diff && diff <= 0.0001f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ValuesNearlyEqual(const float* const values, const U32 numValues)
{
	for (U32F i = 1; i < numValues; ++i)
	{
		if (values[i] >= 0.0f && !NearlyEqual(values[0], values[i]))
			return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static SphericalCoords VectorSphCoordInterp(const SphericalCoords& from, const SphericalCoords& to, float tt)
{
	Vec2 interp = Lerp(from.AsVec2(), to.AsVec2(), tt);
	return SphericalCoords::FromThetaPhi(interp.x, interp.y);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void DebugDrawSphereSurfaceSeg(PrimServerWrapper& ps,
									  Point_arg centerLs,
									  const SphericalCoords& dirA,
									  const SphericalCoords& dirB,
									  const SphericalCoords& dirC,
									  float lenA,
									  float lenB,
									  float lenC,
									  Color clr)
{
	const float distBC = Dist(dirB.AsVec2(), dirC.AsVec2());
	const float distAB = Dist(dirA.AsVec2(), dirB.AsVec2());
	const float distAC = Dist(dirA.AsVec2(), dirC.AsVec2());
	const U32F kNumStepsBC = Max(3.0f, Max(Max(distAB, distAC), distBC) / 180.0f * 20.0f);
	const float invStepsBC = 1.0f / float(kNumStepsBC);

	const U32F kNumStepsA = kNumStepsBC;
	const float invStepsA = 1.0f / float(kNumStepsBC);

	for (U32F i = 0; i < kNumStepsBC; ++i)
	{
		const float u = float(i) * invStepsBC;
		const float un = float(i + 1) * invStepsBC;

		const float midLen = Lerp(lenB, lenC, u);
		const float midLenN = Lerp(lenB, lenC, un);

		const SphericalCoords midTarg = VectorSphCoordInterp(dirB, dirC, u);
		const SphericalCoords midTargN = VectorSphCoordInterp(dirB, dirC, un);

		for (U32F j = 0; j < kNumStepsA; ++j)
		{
			const float v = float(j) * invStepsA;
			const float vn = float(j + 1) * invStepsA;

			const SphericalCoords v0 = VectorSphCoordInterp(dirA, midTargN, vn);
			const SphericalCoords v1 = VectorSphCoordInterp(dirA, midTargN, v);
			const SphericalCoords v2 = VectorSphCoordInterp(dirA, midTarg, v);
			const SphericalCoords v3 = VectorSphCoordInterp(dirA, midTarg, vn);

			const float len0 = Lerp(lenA, midLenN, vn);
			const float len1 = Lerp(lenA, midLenN, v);
			const float len2 = Lerp(lenA, midLen, v);
			const float len3 = Lerp(lenA, midLen, vn);

			const Point p0 = centerLs + (v0.ToUnitVector() * (1.0f + len0));
			const Point p1 = centerLs + (v1.ToUnitVector() * (1.0f + len1));
			const Point p2 = centerLs + (v2.ToUnitVector() * (1.0f + len2));
			const Point p3 = centerLs + (v3.ToUnitVector() * (1.0f + len3));

			ps.DrawQuad(p0, p1, p2, p3, clr);
			//ps.DrawTriangle(p0, p3, p2, clr);
			//ps.DrawTriangle(p0, p2, p3, clr);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void DebugDrawSphereCoordArc(PrimServerWrapper& ps,
									Point_arg centerLs,
									const SphericalCoords& dirA,
									const SphericalCoords& dirB,
									const float lenA,
									const float lenB,
									Color clr)
{
	const U32F kNumSteps = 30;
	const float invSteps = 1.0f / float(kNumSteps);

	for (U32F i = 0; i < kNumSteps; ++i)
	{
		const float u = float(i) * invSteps;
		const float un = float(i + 1) * invSteps;
		const SphericalCoords midTarg = VectorSphCoordInterp(dirA, dirB, u);
		const SphericalCoords midTargN = VectorSphCoordInterp(dirA, dirB, un);
		const float lenMid = Lerp(lenA, lenB, u);
		const float lenMidN = Lerp(lenA, lenB, un);
		const Point p0 = centerLs + (midTarg.ToUnitVector() * (1.0f + lenMid));
		const Point p1 = centerLs + (midTargN.ToUnitVector() * (1.0f + lenMidN));
		ps.DrawLine(p0, p1, clr, clr);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Aabb MakeAabb(Point_arg a, Point_arg b)
{
	Aabb ret;
	ret.IncludePoint(a);
	ret.IncludePoint(b);
	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Locator GetGestureSpaceLocatorOs(const BoundFrame& ownerLoc,
										const FgAnimData* pAnimData,
										const GestureCache::CachedSpace& spaceDef)
{
	if (!pAnimData || (spaceDef.m_jointId == INVALID_STRING_ID_64))
	{
		return kIdentity;
	}

	const I32F iJoint = pAnimData->FindJoint(spaceDef.m_jointId);

	if ((iJoint < 0) || (iJoint >pAnimData->m_jointCache.GetNumAnimatedJoints()))
	{
		return kIdentity;
	}

	const Locator alignWs = ownerLoc.GetLocatorWs();
	const Locator jointWs = pAnimData->m_jointCache.GetJointLocatorWs(iJoint);
	const Locator spaceLocWs = jointWs.TransformLocator(spaceDef.m_offsetLs);

	const Locator spaceLocOs = alignWs.UntransformLocator(spaceLocWs);

	return spaceLocOs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float GetGestureFeedbackStrength(const DC::GestureDef* pGesture)
{
	float str = 0.0f;

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_feedbackStrengthOverride >= 0.0f))
	{
		str = g_animOptions.m_gestures.m_feedbackStrengthOverride;
	}
	else if (pGesture)
	{
		str = pGesture->m_feedbackStrength;
	}

	return str;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float GetSpringK(const DC::GestureState* pGestureState, const DC::GestureDef* pDcDef)
{
	float k = -1.0f;

	switch (pDcDef->m_type.GetValue())
	{
	case SID_VAL("aim"):
		k = Gesture::kDefaultAimGestureSpringConstant;
		break;

	default:
		k = Gesture::kDefaultGestureSpringConstant;
	}

	if (pDcDef && pDcDef->m_defaultPlayParams.m_springConstantValid)
	{
		k = pDcDef->m_defaultPlayParams.m_springConstant;
	}

	if (pGestureState && pGestureState->m_springConstant >= 0.0f)
	{
		k = pGestureState->m_springConstant;
	}

	return k;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float GetSpringKReduced(const DC::GestureState* pGestureState, const DC::GestureDef* pDcDef, float defaultK)
{
	float k = defaultK;

	if (pDcDef && pDcDef->m_defaultPlayParams.m_springConstantReducedValid)
	{
		k = pDcDef->m_defaultPlayParams.m_springConstantReduced;
	}

	if (pGestureState && pGestureState->m_springConstantReduced >= 0.0f)
	{
		k = pGestureState->m_springConstantReduced;
	}

	return k;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float GetSpringDampingRatio(const DC::GestureState* pGestureState, const DC::GestureDef* pDcDef)
{
	if (pGestureState)
	{
		return pGestureState->m_springDampingRatio;
	}

	if (pDcDef && pDcDef->m_defaultPlayParams.m_springDampingRatioValid)
	{
		return pDcDef->m_defaultPlayParams.m_springDampingRatio;
	}

	return Gesture::kDefaultDampingRatio;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Gesture::AnimType GetGestureTypeFromNode(const DC::GestureAnims* pGestureAnims)
{
	bool hasAddAnims = false;
	bool hasSlerpAnims = false;

	if (!pGestureAnims)
		return Gesture::AnimType::kUnknown;

	for (U32F iAnim = 0; iAnim < pGestureAnims->m_numAnimPairs; ++iAnim)
	{
		const DC::GestureAnimPair& animPair = pGestureAnims->m_animPairs[iAnim];

		if ((animPair.m_additiveAnim != INVALID_STRING_ID_64)
			&& (animPair.m_additiveAnim != SID("null-add")))
		{
			hasAddAnims = true;
		}

		if (animPair.m_partialAnim != INVALID_STRING_ID_64)
		{
			hasSlerpAnims = true;
		}
	}

	Gesture::AnimType ret = Gesture::AnimType::kUnknown;

	if (hasSlerpAnims && hasAddAnims)
	{
		ret = Gesture::AnimType::kCombo;
	}
	else if (hasSlerpAnims)
	{
		ret = Gesture::AnimType::kSlerp;
	}
	else if (hasAddAnims)
	{
		ret = Gesture::AnimType::kAdditive;
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static StringId64 GetProxyLocatorId(const DC::GestureDef* pDcDef, StringId64 typeId)
{
	if (pDcDef->m_proxyLocatorId != INVALID_STRING_ID_64)
	{
		return pDcDef->m_proxyLocatorId;
	}

	StringId64 locId = INVALID_STRING_ID_64;
	switch (typeId.GetValue())
	{
	case SID_VAL("look"):
	case SID_VAL("look-animated"):
		locId = SID("apReference-Look");
		break;
	case SID_VAL("aim"):
		locId = SID("apReference-Aim");
		break;
	}
	return locId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void BlendChannelJoint(ndanim::JointParams* pOutChannelJoint,
							  const ndanim::JointParams& left,
							  const ndanim::JointParams& right,
							  float blend,
							  bool radial)
{
	const bool isBlendCloseToZero = IsClose(blend, 0.0f, 0.0001f);

	if (isBlendCloseToZero)
	{
		*pOutChannelJoint = left;
	}
	else
	{
		if (radial)
		{
			AnimChannelJointBlendRadial(pOutChannelJoint, left, right, blend);
		}
		else
		{
			AnimChannelJointBlend(pOutChannelJoint, left, right, blend);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
IGestureNode::IGestureNode(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex)
	: AnimSnapshotNode(typeId, dcTypeId, nodeIndex)
	, m_resolvedGestureId(INVALID_STRING_ID_64)
	, m_pTargetFunc(nullptr)
	, m_nodeUniqueId(-1)
	, m_currentIslandIndex(0)
	, m_inputGestureId(INVALID_STRING_ID_64)
	, m_requestedAltAnims(Gesture::kAlternativeIndexUnspecified)
	, m_requestedAltNoise(Gesture::kAlternativeIndexUnspecified)
	, m_detachedPhase(0.0f)
	, m_pCacheData(nullptr)
	, m_ownerLoc(kIdentity)
	, m_gestureSpaceOs(kIdentity)
	, m_prevGestureSpaceOs(kIdentity)
	, m_fixupTargetDeltaOs(kIdentity)
	, m_noiseQuat(kIdentity)
	, m_constrainedTargetPosLs(kOrigin)
	, m_goalAnglesLs()
	, m_sprungAnglesLs()
	, m_springK(0.0f)
	, m_springKReduced(0.0f)
	, m_springDampingRatio(1.0f)
	, m_feedbackEffect(0.0f)
	, m_sprungAngleDelay(-1.0f)
	, m_firstFrame(false)
	, m_targetValid(false)
	, m_mirroredAnims(false)
	, m_baseAnimIndex(SnapshotNodeHeap::kOutOfMemoryIndex)
	, m_gestureOriginOs(kOrigin)
	, m_prevGestureOriginOs(kOrigin)
{
	m_hSpringLs.Reset();
	m_vSpringLs.Reset();
	m_feedbackEffSpring.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct ResolvedGestureInfo
{
	NdGameObject* m_pOwner = nullptr;
	const IGestureNode* m_pInheritSource = nullptr;

	StringId64 m_inputGestureId;
	StringId64 m_resolvedGestureId;
	ScriptPointer<DC::GestureDef> m_dcDef;

	const DC::AnimNodeGesture* m_pDcGestureNode;
	const DC::GestureAnims* m_pDcGestureAnims;
	const DC::GestureState* m_pGestureState;

	Gesture::AnimType m_gestureType;
	Gesture::AlternativeIndex m_iAlternative;

	bool m_mirror = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ResolveGesture(const DC::AnimNodeGesture* pDcGestureNode,
						   const SnapshotAnimNodeTreeParams& params,
						   ResolvedGestureInfo* pInfoOut,
						   IStringBuilder* pErrorStringOut)
{
	ANIM_ASSERT(pDcGestureNode);
	ANIM_ASSERT(pInfoOut);

	ResolvedGestureInfo& info = *pInfoOut;

	info.m_pOwner = params.m_pAnimData ? NdGameObject::FromProcess(params.m_pAnimData->m_hProcess.ToMutableProcess())
									   : nullptr;
	info.m_pInheritSource = nullptr;

	info.m_inputGestureId = Gesture::GetInputGestureId(pDcGestureNode, params);
	info.m_resolvedGestureId = info.m_inputGestureId;

	if (pDcGestureNode->m_resolveGestureIdInOverlays)
	{
		if (params.m_pMutableOverlaySnapshot)
		{
			info.m_resolvedGestureId = params.m_pMutableOverlaySnapshot->LookupTransformedAnimIdAndIncrementVariantIndices(info.m_resolvedGestureId);
		}
		else if (params.m_pConstOverlaySnapshot)
		{
			info.m_resolvedGestureId = params.m_pConstOverlaySnapshot->LookupTransformedAnimId(info.m_resolvedGestureId);
		}
	}

	if ((info.m_resolvedGestureId == SID("null") || (info.m_resolvedGestureId == INVALID_STRING_ID_64)))
	{
		if (FALSE_IN_FINAL_BUILD(pErrorStringOut))
		{
			pErrorStringOut->append_format("[%s] gesture remapped to null",
										   DevKitOnly_StringIdToString(info.m_resolvedGestureId));
		}

		return false;
	}

	info.m_dcDef = ScriptPointer<DC::GestureDef>(info.m_resolvedGestureId, SID("gestures"));

	const DC::GestureDef* pDcGesture = info.m_dcDef;

	if (nullptr == pDcGesture)
	{
		if (FALSE_IN_FINAL_BUILD(pErrorStringOut))
		{
			pErrorStringOut->append_format("[%s] remapped gesture '%s' not found",
										   DevKitOnly_StringIdToString(info.m_inputGestureId),
										   DevKitOnly_StringIdToString(info.m_resolvedGestureId));
		}

		return false;
	}

	const DC::GestureAnims* pDcGestureAnims = pDcGesture;
	const DC::GestureState* pGestureState = nullptr;

	if (pDcGestureNode->m_gestureStateFunc)
	{
		ScriptValue argv[] =
		{
			ScriptValue(params.m_pInfoCollection),
		};

		const ScriptValue res = ScriptManager::Eval(pDcGestureNode->m_gestureStateFunc, ARRAY_COUNT(argv), argv);
		pGestureState = (const DC::GestureState*)res.m_pointer;
	}

	Gesture::AlternativeIndex iAlt = pGestureState ? pGestureState->m_alternativeIndex : Gesture::kAlternativeIndexUnspecified;

	GestureCache::CacheKey newCacheKey;
	newCacheKey.m_gestureId = info.m_resolvedGestureId;
	info.m_pInheritSource = FindGestureNodeToInheritFrom(newCacheKey, BoundFrame(kIdentity), false, params);

	if (iAlt == Gesture::kAlternativeIndexUnspecified && info.m_pInheritSource)
	{
		iAlt = info.m_pInheritSource->GetRequestedAlternative();
	}

	const FactDictionary* pFacts = info.m_pOwner ? info.m_pOwner->UpdateAndGetFactDict() : nullptr;
	const DC::FactMap* pAlternatives = pDcGesture->m_gestureAlternatives;
	const DC::GestureAlternative* pChosenAlternative = nullptr;

	if (iAlt == Gesture::kAlternativeIndexUnspecified)
	{
		const bool debug = FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_debugGestureAlternatives
												&& g_animOptions.IsGestureOrNoneSelected(pDcGesture->m_name)
												&& DebugSelection::Get().IsProcessOrNoneSelected(info.m_pOwner));

		iAlt = Gesture::DesiredAlternative(pDcGesture, pFacts, pChosenAlternative, debug);
	}
	else if (pAlternatives && (iAlt >= 0) && (iAlt < pAlternatives->m_numEntries))
	{
		pChosenAlternative = (const DC::GestureAlternative*)pAlternatives->m_values[iAlt].m_ptr;
	}

	if (pChosenAlternative)
	{
		if (pChosenAlternative->m_isKillGesture)
		{
			if (FALSE_IN_FINAL_BUILD(pErrorStringOut))
			{
				pErrorStringOut->append_format("[%s] Alternative %d is a kill gesture",
											   DevKitOnly_StringIdToString(info.m_resolvedGestureId),
											   iAlt);
			}

			return false;
		}

		pDcGestureAnims = pChosenAlternative;
	}

	info.m_iAlternative	   = iAlt;
	info.m_gestureType	   = GetGestureTypeFromNode(pDcGestureAnims);
	info.m_pDcGestureNode  = pDcGestureNode;
	info.m_pDcGestureAnims = pDcGestureAnims;
	info.m_pGestureState   = pGestureState;
	info.m_mirror = pDcGestureAnims && pDcGestureAnims->m_mirrored;

	if (params.m_stateFlipped && pDcGestureAnims->m_detachedMirror)
	{
		info.m_mirror = !info.m_mirror;
	}

	if (info.m_gestureType == Gesture::AnimType::kUnknown)
	{
		if (FALSE_IN_FINAL_BUILD(pErrorStringOut))
		{
			pErrorStringOut->append_format("[%s] Unable to determine gesture anim type.",
										   DevKitOnly_StringIdToString(info.m_resolvedGestureId));
		}

		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IGestureNode::TrySnapshotNode(AnimStateSnapshot* pSnapshot,
								   const ResolvedGestureInfo& info,
								   const SnapshotAnimNodeTreeParams& params,
								   SnapshotAnimNodeTreeResults& results,
								   IStringBuilder* pErrorStringOut /* = nullptr */)
{
	PROFILE_ACCUM(GestureNode_TrySnapshotNode);

	const NdGameObject* pOwner = info.m_pOwner;

	m_nodeUniqueId = s_gestureNodeUniqueId.Add(1);

	m_inputGestureId	= info.m_inputGestureId;
	m_resolvedGestureId = info.m_resolvedGestureId;

	m_requestedAltAnims = Gesture::kAlternativeIndexUnspecified;
	m_requestedAltNoise = Gesture::kAlternativeIndexUnspecified;

	m_dcDef = info.m_dcDef;

	const DC::GestureDef* pDcDef = GetDcDef();
	if (pDcDef && pDcDef->m_weaponIkFeather)
	{
		results.m_newStateFlags |= DC::kAnimStateFlagWeaponIkFeatherVariable;
		results.m_newStateFlags |= DC::kAnimStateFlagNoWeaponIk;
	}

	m_useLegFixIk  = false;
	m_handFixIkMask = 0;

	m_baseLayerGesture = params.m_pFadeToStateParams && params.m_pFadeToStateParams->m_isBase;
	m_pTargetFunc	   = info.m_pDcGestureNode ? info.m_pDcGestureNode->m_gestureTargetFunc : nullptr;

	const IGestureController* pGestureController = pOwner ? pOwner->GetGestureController() : nullptr;

	if (!pGestureController)
	{
		if (FALSE_IN_FINAL_BUILD(pErrorStringOut))
		{
			pErrorStringOut->append_format("[%s] no owner process", DevKitOnly_StringIdToString(m_inputGestureId));
		}

		return false;
	}

	const DC::GestureDef* pDcGesture = m_dcDef;
	const DC::GestureAnims* pGestureAnims = Gesture::GetGestureAnims(pDcGesture, info.m_iAlternative);

	m_mirroredAnims = info.m_mirror;

	m_cacheKey.m_gestureId = m_resolvedGestureId;
	m_cacheKey.m_skelId	   = params.m_pAnimTable->GetSkelId();
	m_cacheKey.m_altIndex  = info.m_iAlternative;
	m_cacheKey.m_gestureNodeType = info.m_gestureType;
	m_cacheKey.m_flipped		 = params.m_stateFlipped ^ m_mirroredAnims;

	m_pCacheData = g_gestureCache.TryCacheData(m_cacheKey, pDcGesture, params.m_pAnimTable->GetHierarchyId());

	if (!m_pCacheData)
	{
		if (FALSE_IN_FINAL_BUILD(pErrorStringOut))
		{
			pErrorStringOut->append_format("[%s] Gesture Cache is full! Tell a programmer",
										   DevKitOnly_StringIdToString(m_resolvedGestureId));
		}

		return false;
	}

	if ((m_pCacheData->m_numGestureAnims == 0) && !m_pCacheData->m_isBad)
	{
		if (FALSE_IN_FINAL_BUILD(pErrorStringOut))
		{
			pErrorStringOut->append_format("[%s] zero gesture anim pairs",
										   DevKitOnly_StringIdToString(m_resolvedGestureId));
		}

		const I64 prevVal = m_pCacheData->m_referenceCount.Add(-1);

		ANIM_ASSERTF(prevVal > 0, ("%d", prevVal));

		return false;
	}

	m_detachedPhase = 0.0f;

	if (pGestureAnims && pGestureAnims->m_disableWeaponIk)
	{
		results.m_newStateFlags |= DC::kAnimStateFlagNoWeaponIk;
	}

	m_ownerLoc				= pOwner->GetBoundFrame();
	m_springK				= GetSpringK(info.m_pGestureState, pDcGesture);
	m_springKReduced		= GetSpringKReduced(info.m_pGestureState, pDcGesture, m_springK);
	m_springDampingRatio	= GetSpringDampingRatio(info.m_pGestureState, pDcGesture);

	if (info.m_pGestureState)
	{
		m_hProp = NdGameObjectHandle::FromProcessHandle(info.m_pGestureState->m_propHandle);

		m_useLegFixIk  = info.m_pGestureState->m_useLegFixIk;
		m_handFixIkMask = info.m_pGestureState->m_handFixIkMask;
	}
	else
	{
		m_hProp = NdGameObjectHandle();

		if (pDcGesture->m_defaultPlayParams.m_legFixIkValid)
		{
			m_useLegFixIk = pDcGesture->m_defaultPlayParams.m_legFixIk;
		}

		if (pDcGesture->m_defaultPlayParams.m_handFixIkValid)
		{
			m_handFixIkMask = pDcGesture->m_defaultPlayParams.m_handFixIk;
		}
	}

	m_hSpringLs.Reset();
	m_vSpringLs.Reset();

	m_feedbackEffect = 0.0f;
	m_feedbackEffSpring.Reset();

	m_sprungAngleDelay = -1.0f;

	m_goalAnglesLs = m_sprungAnglesLs = SphericalCoords::FromThetaPhi(0.0f, 0.0f);

	m_targetValid = false;
	m_targetPosPs = kOrigin;
	m_targetMgrSlot = -1;

	m_feedbackError	   = kIdentity;
	m_filteredFeedback = kIdentity;
	m_proxyRotLs	   = kIdentity;
	m_noiseQuat		   = kIdentity;

	m_firstFrame = true;

	const IGestureNode* pInheritSource = info.m_pInheritSource;
	if (pInheritSource && pInheritSource->IsContiguousWith(m_cacheKey, m_ownerLoc, true))
	{
		m_goalAnglesLs	 = pInheritSource->m_goalAnglesLs;
		m_sprungAnglesLs = pInheritSource->m_sprungAnglesLs;

		ANIM_ASSERT(IsReasonable(m_goalAnglesLs));
		ANIM_ASSERT(IsReasonable(m_sprungAnglesLs));

		m_hSpringLs = pInheritSource->m_hSpringLs;
		m_vSpringLs = pInheritSource->m_vSpringLs;

		m_feedbackEffect	= pInheritSource->m_feedbackEffect;
		m_feedbackEffSpring = pInheritSource->m_feedbackEffSpring;

		m_islandTransition = pInheritSource->m_islandTransition;
		m_currentIslandIndex = pInheritSource->m_currentIslandIndex;

		m_detachedPhase	   = pInheritSource->m_detachedPhase;
		m_gestureSpaceOs   = pInheritSource->m_gestureSpaceOs;
		m_feedbackError	   = pInheritSource->m_feedbackError;
		m_filteredFeedback = pInheritSource->m_filteredFeedback;
		m_proxyRotLs	   = pInheritSource->m_proxyRotLs;
		m_firstFrame	   = pInheritSource->m_firstFrame;
		m_noiseQuat		   = pInheritSource->m_noiseQuat;

		m_prevGestureSpaceOs  = pInheritSource->m_prevGestureSpaceOs;
		m_prevGestureOriginOs = pInheritSource->m_prevGestureOriginOs;

		m_constrainedTargetPosLs = pInheritSource->m_constrainedTargetPosLs;

		m_prevInputAnglesLs = pInheritSource->m_prevInputAnglesLs;
		m_prevAnglesLs		= pInheritSource->m_prevAnglesLs;

		m_targetMgrSlot		 = pInheritSource->m_targetMgrSlot;
		m_targetPosPs		 = pInheritSource->m_targetPosPs;
		m_targetValid		 = pInheritSource->m_targetValid;
		m_gestureOriginOs	 = pInheritSource->m_gestureOriginOs;
		m_fixupTargetDeltaOs = pInheritSource->m_fixupTargetDeltaOs;

		m_sprungAngleDelay = pInheritSource->m_sprungAngleDelay;
	}
	else
	{
		m_gestureSpaceOs = GetGestureSpaceLocatorOs(m_ownerLoc, params.m_pAnimData, m_pCacheData->m_animSpace);
		m_prevGestureSpaceOs = m_gestureSpaceOs;

		if (m_pCacheData->m_originSpace.m_jointId != INVALID_STRING_ID_64)
		{
			const Locator originLocOs = GetGestureSpaceLocatorOs(m_ownerLoc,
																 params.m_pAnimData,
																 m_pCacheData->m_originSpace);
			m_gestureOriginOs = originLocOs.Pos();
		}
		else
		{
			m_gestureOriginOs = pOwner->GetGestureOriginOs(m_resolvedGestureId, m_pCacheData->m_typeId);
		}

		m_prevGestureOriginOs = m_gestureOriginOs;

		RefreshFixupTarget(pSnapshot, params.m_pInfoCollection);

		m_prevInputAnglesLs = SphericalCoords::FromThetaPhi(0.0f, 0.0f);
		m_prevAnglesLs		= SphericalCoords::FromThetaPhi(0.0f, 0.0f);

		if (m_pTargetFunc)
		{
			ScriptValue argv[] = { ScriptValue(params.m_pInfoCollection) };
			const ScriptValue pointResult = ScriptManager::Eval(m_pTargetFunc, ARRAY_COUNT(argv), argv);
			const Point* pPointResult = static_cast<const Point*>(pointResult.m_pointer);

			if (pPointResult)
			{
				const Point targetPosWs = *pPointResult;
				m_targetPosPs = m_ownerLoc.GetParentSpace().UntransformPoint(targetPosWs);
				m_targetValid = true;
			}
		}
		else
		{
			const GestureTargetManager& gtm = pGestureController->GetTargetManager();

			m_targetMgrSlot = gtm.FindSlot(m_pCacheData->m_typeId);

			if (const Gesture::Target* pTarget = gtm.GetTarget(m_targetMgrSlot))
			{
				const Locator originWs = m_ownerLoc.GetLocatorWs().TransformLocator(Locator(m_gestureOriginOs, m_gestureSpaceOs.Rot()));

				const Maybe<Point> targetPosWs = pTarget->GetWs(originWs);
				if (targetPosWs.Valid())
				{
					m_targetPosPs = m_ownerLoc.GetParentSpace().UntransformPoint(targetPosWs.Get());
					m_targetValid = true;
				}
			}
		}
	}

	m_loddedOut = pOwner->GetAnimLod() >= DC::kAnimLodFar && (pDcDef->m_type != SID("aim"));

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::ReleaseNodeRecursive(SnapshotNodeHeap* pHeap) const
{
	if (m_pCacheData && !FALSE_IN_FINAL_BUILD(g_gestureCache.DebugEraseInProgress()))
	{
		const I64 prevVal = m_pCacheData->m_referenceCount.Add(-1);

		ANIM_ASSERTF(prevVal > 0, ("%d", prevVal));
	}

	ParentClass::ReleaseNodeRecursive(pHeap);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SnapshotNodeHeap::Index IGestureNode::Clone(SnapshotNodeHeap* pDestHeap, const SnapshotNodeHeap* pSrcHeap) const
{
	if (m_pCacheData)
	{
		m_pCacheData->m_referenceCount.Add(1);
	}

	return ParentClass::Clone(pDestHeap, pSrcHeap);
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F IGestureNode::CalculateBreadth() const
{
	if (!m_pCacheData)
		return 1;

	I32F breadth = Min(m_pCacheData->m_numGestureAnims, I32(2));

	if (m_pCacheData->m_numIslands > 1)
	{
		++breadth;
	}

	return breadth;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U8 IGestureNode::RefreshBreadth(AnimStateSnapshot* pSnapshot)
{
	return CalculateBreadth();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::StepNode(AnimStateSnapshot* pSnapshot, float deltaTime, const DC::AnimInfoCollection* pInfoCollection)
{
	PROFILE_ACCUM(GestureNode_StepNode);

	if (!m_pCacheData)
	{
		return;
	}

	if (m_pTargetFunc)
	{
		ScriptValue argv[] = { ScriptValue(pInfoCollection) };
		const ScriptValue pointResult = ScriptManager::Eval(m_pTargetFunc, ARRAY_COUNT(argv), argv);
		const Point* pPointResult = static_cast<const Point*>(pointResult.m_pointer);

		if (pPointResult)
		{
			const Point targetPosWs = *pPointResult;
			m_targetPosPs = m_ownerLoc.GetParentSpace().UntransformPoint(targetPosWs);
			m_targetValid = true;
		}
		else
		{
			m_targetValid = false;
		}
	}

	if (m_pCacheData->m_useDetachedPhase && (m_pCacheData->m_numGestureAnims > 0))
	{
		I32F iAnim = 0;
		if (IsSingleAnimMode())
		{
			SelectedAnimInfo selInfo;
			SelectGestureAnims(&selInfo, m_currentIslandIndex);

			if (selInfo.m_anims[0] >= 0)
			{
				iAnim = selInfo.m_anims[0];
			}
		}

		ANIM_ASSERT(iAnim >= 0 && iAnim < m_pCacheData->m_numGestureAnims);

		const GestureCache::CachedAnim& anim = m_pCacheData->m_cachedAnims[iAnim];
		const ArtItemAnim* pAnim = anim.m_hSlerpAnim.ToArtItem();

		if (!pAnim)
		{
			pAnim = anim.m_hAddAnim.ToArtItem();
		}

		if (pAnim)
		{
			const ndanim::ClipData* pClipData = pAnim->m_pClipData;

			const float deltaPhase = Limit01(deltaTime * pClipData->m_framesPerSecond * pClipData->m_phasePerFrame);

			const bool isLooping = pAnim->IsLooping();
			const float desPhase = m_detachedPhase + deltaPhase;
			m_detachedPhase = isLooping ? (float)Fmod(desPhase) : Limit01(desPhase);
		}
	}

	if (m_islandTransition.m_transitioning)
	{
		const bool isDone = m_islandTransition.UpdateTransition(deltaTime);

		if (isDone)
		{
			m_currentIslandIndex = m_islandTransition.m_targetIsland;
		}
	}
	else if (m_pCacheData->m_numIslands > 1)
	{
		const SphericalCoords angles = GetAngles();

		const float hTargetDeg = angles.Theta();
		const float vTargetDeg = angles.Phi();

		const I32F tri = FindNearestBlendTriangle(hTargetDeg, vTargetDeg);

		if (tri >= 0)
		{
			const U8 closestIsland = m_pCacheData->m_blendTriIslandIndex[tri];

			m_islandTransition.ClosestIsland(closestIsland, deltaTime);

			if (m_islandTransition.ShouldTransition(m_currentIslandIndex))
			{
				m_islandTransition.BeginTransition(m_islandTransition.m_closestIsland);
			}
		}
	}

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_disableProxyCorrection))
	{
		m_proxyRotLs = kIdentity;
	}
	else if (const DC::GestureDef* pDcDef = GetDcDef())
	{
		m_proxyRotLs = ReadProxyRotLs(pDcDef, pSnapshot, m_baseAnimIndex);
	}
	else
	{
		m_proxyRotLs = kIdentity;
	}

	RefreshFixupTarget(pSnapshot, pInfoCollection);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IGestureNode::RefreshFixupTarget(AnimStateSnapshot* pSnapshot, const DC::AnimInfoCollection* pInfoCollection)
{
	const DC::GestureDef* pDcDef = GetDcDef();
	const DC::GestureFixupIkConfig* pConfig = pDcDef ? pDcDef->m_fixupIk : nullptr;

	if (!pConfig || !m_pCacheData || (m_pCacheData->m_numGestureAnims <= 0))
	{
		m_fixupTargetDeltaOs = kIdentity;
		return false;
	}

	const GestureCache::CachedAnim& firstAnim = m_pCacheData->m_cachedAnims[0];

	const float animPhase	 = (firstAnim.m_phase >= 0.0f) ? firstAnim.m_phase : pSnapshot->m_statePhase;
	const float evalPhase	 = m_pCacheData->m_useDetachedPhase ? m_detachedPhase : animPhase;
	const ArtItemAnim* pAnim = firstAnim.m_hAddAnim.IsNull() ? firstAnim.m_hSlerpAnim.ToArtItem()
															 : firstAnim.m_hAddAnim.ToArtItem();

	if (!pAnim)
	{
		m_fixupTargetDeltaOs = kIdentity;
		return false;
	}

	EvaluateChannelParams params;
	params.m_channelNameId = pConfig->m_constraint.m_channelId;
	params.m_mirror		   = pSnapshot->IsFlipped(pInfoCollection);
	params.m_pAnim		   = pAnim;
	params.m_phase		   = Limit01(evalPhase);

	ndanim::JointParams jpFixupConstraintOs;
	if (!EvaluateChannelInAnim(m_pCacheData->m_key.m_skelId, &params, &jpFixupConstraintOs))
	{
		if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_debugDrawFixupIk))
		{
			MsgConPauseable("Gesture Fixup IK Channel '%s' is not found in the anim '%s' (gesture: '%s')\n",
							DevKitOnly_StringIdToString(params.m_channelNameId),
							pAnim->GetName(),
							DevKitOnly_StringIdToString(m_resolvedGestureId));
		}

		return false;
	}

	params.m_channelNameId = pConfig->m_constraintSrcLoc;

	ndanim::JointParams jpFixupGoalOs;
	if (!EvaluateChannelInAnim(m_pCacheData->m_key.m_skelId, &params, &jpFixupGoalOs))
	{
		if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_debugDrawFixupIk))
		{
			MsgConPauseable("Gesture Fixup IK Channel '%s' is not found in the anim '%s' (gesture: '%s')\n",
							DevKitOnly_StringIdToString(params.m_channelNameId),
							pAnim->GetName(),
							DevKitOnly_StringIdToString(m_resolvedGestureId));
		}

		return false;
	}

	const Locator locConstraintOs = Locator(jpFixupConstraintOs.m_trans, jpFixupConstraintOs.m_quat);
	const Locator locGoalOs	= Locator(jpFixupGoalOs.m_trans, jpFixupGoalOs.m_quat);

	m_fixupTargetDeltaOs = locConstraintOs.UntransformLocator(locGoalOs);

	if (false)
	{
		StringBuilder<256> desc;
		desc.format("constraint: %s [%s]",
					DevKitOnly_StringIdToString(pConfig->m_constraint.m_channelId),
					DevKitOnly_StringIdToString(pConfig->m_constraint.m_jointId));
		g_prim.Draw(DebugCoordAxesLabeled(m_ownerLoc.GetLocatorWs().TransformLocator(locConstraintOs),
										  desc.c_str(),
										  0.3f,
										  kPrimEnableHiddenLineAlpha,
										  2.0f,
										  kColorWhite,
										  0.5f));

		desc.format("goal: %s\ndelta: %s",
					DevKitOnly_StringIdToString(pConfig->m_constraintSrcLoc),
					PrettyPrint(m_fixupTargetDeltaOs));
		g_prim.Draw(DebugCoordAxesLabeled(m_ownerLoc.GetLocatorWs().TransformLocator(locGoalOs),
										  desc.c_str(),
										  0.3f,
										  kPrimEnableHiddenLineAlpha,
										  2.0f,
										  kColorWhite,
										  0.5f));
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Quat IGestureNode::ReadProxyRotLs(const DC::GestureDef* pDcDef, const AnimStateSnapshot* pSnapshot, I32F childIndex) const
{
	const DC::GestureDef* pDcGesture = m_dcDef;

	Quat proxyRotLs = kIdentity;

	const StringId64 proxyLocId = GetProxyLocatorId(pDcDef, m_pCacheData->m_typeId);

	if (proxyLocId != INVALID_STRING_ID_64)
	{
		if (const AnimSnapshotNode* pChildNode = pSnapshot->GetSnapshotNode(childIndex))
		{
			ndanim::JointParams proxyJp;

			const bool animsMirorred = GestureAnimsMirrored();

			SnapshotEvaluateParams evalParams;
			evalParams.m_channelName = proxyLocId;
			evalParams.m_statePhase = pSnapshot->m_statePhase;
			evalParams.m_flipped = pSnapshot->IsFlipped() ^ animsMirorred;

			if (pChildNode->EvaluateChannel(pSnapshot, &proxyJp, evalParams))
			{
				if (false && g_animOptions.IsGestureOrNoneSelected(m_resolvedGestureId))
				{
					StringBuilder<256> desc;
					desc.format("%s [%s]", DevKitOnly_StringIdToString(proxyLocId), pChildNode->GetName());
					const Locator proxyWs = m_ownerLoc.GetLocatorWs().TransformLocator(Locator(proxyJp.m_trans,
																							   proxyJp.m_quat));
					g_prim.Draw(DebugCoordAxesLabeled(proxyWs,
													  desc.c_str(),
													  0.3f,
													  kPrimEnableHiddenLineAlpha,
													  3.0f,
													  kColorWhite,
													  0.5f));
				}

				const Locator proxyLocLs = m_gestureSpaceOs.UntransformLocator(Locator(proxyJp.m_trans, proxyJp.m_quat));
				proxyRotLs = proxyLocLs.Rot();
			}
		}
	}


	return proxyRotLs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ndanim::ValidBits IGestureNode::GetValidBits(const ArtItemSkeleton* pSkel,
											 const AnimStateSnapshot* pSnapshot,
											 U32 iGroup) const
{
	PROFILE_ACCUM(GestureNode_GetValidBits);

	if ((m_resolvedGestureId == INVALID_STRING_ID_64) || !m_pCacheData)
	{
		return ndanim::ValidBits(0ULL, 0ULL);
	}

	const bool wantAdd = IsAdditive();

	for (U32F i = 0; i < m_pCacheData->m_numGestureAnims; ++i)
	{
		if (const ArtItemAnim* pAnim = GetAnimFromIndex(i, wantAdd))
		{
			return GetRetargetedValidBits(pSkel, pAnim, iGroup);
		}
	}

	return ndanim::ValidBits(0ULL, 0ULL);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::PostRootLocatorUpdate(NdGameObject* pOwner,
										 float deltaTime,
										 float statePhase,
										 float combinedBlend,
										 float feedbackBlend)
{
	const Gesture::Target* pTarget = nullptr;

	if (m_targetMgrSlot >= 0)
	{
		const IGestureController* pGestureController = pOwner->GetGestureController();
		pTarget = pGestureController ? pGestureController->GetTargetManager().GetTarget(m_targetMgrSlot) : nullptr;
	}

	PostRootLocatorUpdate(pOwner, deltaTime, statePhase, combinedBlend, feedbackBlend, pTarget);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::PostRootLocatorUpdate(NdGameObject* pOwner,
										 float deltaTime,
										 float statePhase,
										 float combinedBlend,
										 float feedbackBlend,
										 const Gesture::Target* pTarget)
{
	const DC::GestureDef* pDcDef = GetDcDef();

	if (!m_pCacheData || !pDcDef)
	{
		return;
	}

	const bool sameBinding = m_ownerLoc.IsSameBinding(pOwner->GetBoundFrame().GetBinding());

	const Locator prevLocPs = m_ownerLoc.GetLocatorPs();

	m_ownerLoc	= pOwner->GetBoundFrame();

	if ((m_pCacheData->m_originSpace.m_jointId == INVALID_STRING_ID_64) && !IsSingleAnimMode())
	{
		m_prevGestureOriginOs = m_gestureOriginOs;
		m_gestureOriginOs = pOwner->GetGestureOriginOs(m_resolvedGestureId, m_pCacheData->m_typeId);
	}

	if (sameBinding && !IsSingleAnimMode() && TRUE_IN_FINAL_BUILD(!g_animOptions.m_gestures.m_disableExternalCompensation))
	{
		const Locator newLocPs = m_ownerLoc.GetLocatorPs();

		// TODO: There's a way to handle external origin translation affecting the sprung angles as well
		const Locator prevGestureLocPs = prevLocPs.TransformLocator(m_prevGestureSpaceOs);
		const Locator newGestureLocPs = newLocPs.TransformLocator(m_gestureSpaceOs);

		const Vector prevGestureDirPs = GetLocalZ(prevGestureLocPs);
		const Vector newGestureDirPs = GetLocalZ(newGestureLocPs);

		const float xzDiffRad = GetRelativeXzAngleDiffRad(prevGestureDirPs, newGestureDirPs);
		const float xzDiffDeg = RADIANS_TO_DEGREES(xzDiffRad);

		const float yDiffRad = SafeAsin(newGestureDirPs.Y()) - SafeAsin(prevGestureDirPs.Y());
		const float yDiffDeg = RADIANS_TO_DEGREES(yDiffRad);

		ANIM_ASSERT(IsReasonable(xzDiffDeg));
		ANIM_ASSERT(IsReasonable(yDiffDeg));

		if (false /*g_animOptions.IsGestureOrNoneSelected(m_resolvedGestureId)*/)
		{
			MsgCon("prev space: %s\n", PrettyPrint(m_prevGestureSpaceOs));
			MsgCon(" new space: %s\n", PrettyPrint(m_gestureSpaceOs));
			MsgCon(" delta: %0.1f / %0.1f\n", xzDiffDeg, yDiffDeg);
			MsgCon(" speed: %0.1f / %0.1f\n", xzDiffDeg / deltaTime, yDiffDeg / deltaTime);

			g_prim.Draw(DebugCoordAxes(prevGestureLocPs, 0.25f, kPrimDisableDepthTest, 4.0f));
			g_prim.Draw(DebugCoordAxes(newGestureLocPs, 0.5f, kPrimDisableDepthTest));
		}

		m_sprungAnglesLs.Adjust(xzDiffDeg, -yDiffDeg);

		ANIM_ASSERT(IsReasonable(m_sprungAnglesLs));
	}

	if (pDcDef->m_spacesUsePrevFrame && !IsSingleAnimMode())
	{
		const JointCache* pJointCache = &pOwner->GetAnimData()->m_jointCache;
		const Locator& alignWs = m_ownerLoc.GetLocatorWs();

		if (m_pCacheData->m_animSpace.m_jointId != INVALID_STRING_ID_64)
		{
			m_prevGestureSpaceOs = m_gestureSpaceOs;
			m_gestureSpaceOs	 = kIdentity;

			const I32F iJoint = pOwner->FindJointIndex(m_pCacheData->m_animSpace.m_jointId);

			if (iJoint >= 0)
			{
				const Locator jointLocWs = pJointCache->GetJointLocatorWs(iJoint);
				const Locator jointLocOs = alignWs.UntransformLocator(jointLocWs);

				m_gestureSpaceOs = jointLocOs.TransformLocator(m_pCacheData->m_animSpace.m_offsetLs);
			}
		}

		if (m_pCacheData->m_originSpace.m_jointId != INVALID_STRING_ID_64)
		{
			m_prevGestureOriginOs = m_gestureOriginOs;
			m_gestureOriginOs = kOrigin;

			const I32F iJoint = pOwner->FindJointIndex(m_pCacheData->m_originSpace.m_jointId);

			if (iJoint >= 0)
			{
				const Locator jointLocWs = pJointCache->GetJointLocatorWs(iJoint);
				const Locator jointLocOs = alignWs.UntransformLocator(jointLocWs);

				m_gestureOriginOs = jointLocOs.TransformLocator(m_pCacheData->m_originSpace.m_offsetLs).Pos();
			}
		}

		const bool enableFeedback = !pDcDef->m_disableFeedback
									&& (m_pCacheData->m_feedbackSpace.m_jointId != INVALID_STRING_ID_64)
									&& !IsSingleAnimMode();

		if (enableFeedback)
		{
			const I32F iJoint = pOwner->FindJointIndex(m_pCacheData->m_feedbackSpace.m_jointId);

			if (iJoint >= 0)
			{
				const Locator jointLocWs = pJointCache->GetJointLocatorWs(iJoint);
				const Locator jointLocOs = alignWs.UntransformLocator(jointLocWs);

				const I32F iOriginJoint = (pDcDef && pDcDef->m_feedbackOrigin)
											  ? pOwner->FindJointIndex(pDcDef->m_feedbackOrigin->m_jointId)
											  : -1;

				if (iOriginJoint >= 0)
				{
					const Locator offsetLs = pDcDef->m_feedbackOrigin->m_offset ? *pDcDef->m_feedbackOrigin->m_offset : Locator(kIdentity);
					const Locator originLocWs = pJointCache->GetJointLocatorWs(iOriginJoint).TransformLocator(offsetLs);
					const Locator originLocOs = alignWs.UntransformLocator(originLocWs);
					
					HandleFeedback(jointLocOs, &originLocOs);
				}
				else
				{
					HandleFeedback(jointLocOs);
				}
			}
		}
	}

	const Locator& alignPs = m_ownerLoc.GetLocatorPs();
	const Point gestureOriginLs = m_gestureSpaceOs.UntransformPoint(m_gestureOriginOs);

	m_targetValid = false;

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_forceTowardsCamera
							 && g_animOptions.IsGestureOrNoneSelected(m_resolvedGestureId)))
	{
		const RenderCamera& cam = GetRenderCamera(0);
		const Point targetPosWs = cam.GetPosition();
		m_targetPosPs = m_ownerLoc.GetParentSpace().UntransformPoint(targetPosWs);
		m_targetValid = true;
	}
	else if (pTarget)
	{
		const Locator originWs = m_ownerLoc.GetLocatorWs().TransformLocator(Locator(m_gestureOriginOs, m_gestureSpaceOs.Rot()));
		const Maybe<Point> targetPosWs = pTarget->GetWs(originWs);

		if (targetPosWs.Valid())
		{
			m_targetPosPs = m_ownerLoc.GetParentSpace().UntransformPoint(targetPosWs.Get());
			m_targetValid = true;
		}
	}

	Vector inputGoalDirLs = kUnitZAxis;
	Point inputTargetPosLs = Point(0.0f, 0.0f, 10.0f);

	if (m_targetValid)
	{
		const Point inputTargetPosOs = alignPs.UntransformPoint(m_targetPosPs);
		inputTargetPosLs = m_gestureSpaceOs.UntransformPoint(inputTargetPosOs);
		inputGoalDirLs = SafeNormalize(inputTargetPosLs - gestureOriginLs, kZero);

		const Point prevInputTargetPosOs = alignPs.UntransformPoint(m_targetPosPs);
		const Point prevInputTargetPosLs = m_gestureSpaceOs.UntransformPoint(prevInputTargetPosOs);
		const Vector prevInputGoalDirLs = SafeNormalize(prevInputTargetPosLs - gestureOriginLs, kZero);
		const float inputAngleDiffDeg = RADIANS_TO_DEGREES(SafeAcos(Dot(prevInputGoalDirLs, inputGoalDirLs)));

		if ((pDcDef->m_angleDiffDelayThresholdDeg >= 0.0f) && (inputAngleDiffDeg >= pDcDef->m_angleDiffDelayThresholdDeg))
		{
			m_sprungAngleDelay = pDcDef->m_angleDiffDelayTimeSec + deltaTime;
		}
	}

	const float feedbackStrength = GetGestureFeedbackStrength(pDcDef);

	const float desFeedbackLevel = (Max(combinedBlend - 0.5f, 0.0f) * 2.0f) * feedbackBlend;

	const float feedbackEffSpringK = 5.0f;
	m_feedbackEffect = m_feedbackEffSpring.Track(m_feedbackEffect,
												 desFeedbackLevel,
												 deltaTime,
												 feedbackEffSpringK);

#if 1
	m_feedbackEffect = Min(m_feedbackEffect, feedbackBlend);

	m_filteredFeedback = Slerp(kIdentity, m_filteredFeedback, combinedBlend);
	m_feedbackError	   = Slerp(kIdentity, m_feedbackError, combinedBlend);
#else
	m_feedbackEffect = Max(m_feedbackEffect, feedbackBlend);
#endif

	if (feedbackStrength > 0.0f)
	{
		const float tt = Limit01(deltaTime * feedbackStrength);
		m_filteredFeedback = Normalize(Slerp(m_filteredFeedback, m_feedbackError, tt));
	}
	else
	{
		m_filteredFeedback = m_feedbackError;
	}

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_disableGestureFeedback))
	{
		m_filteredFeedback = kIdentity;
		m_feedbackError	   = kIdentity;
	}
	else
	{
		inputGoalDirLs = Rotate(Conjugate(m_filteredFeedback), inputGoalDirLs);
	}

	const Character* pOwnerChar = Character::FromProcess(pOwner);

	if (pOwnerChar && pOwnerChar->IsDead())
	{
		m_noiseQuat = kIdentity;
	}
	else
	{
		m_noiseQuat = GetNoiseQuat();
	}

	inputGoalDirLs = Rotate(m_noiseQuat, inputGoalDirLs);

	const SphericalCoords inputAngles = GetInputAngles(inputGoalDirLs);

	if (pDcDef->m_blinkOnAngleDiff && (pDcDef->m_blinkAngleDiffDeg >= 0.0f) && !m_firstFrame)
	{
		if (BlinkController* pBlinkController = pOwner->GetBlinkController())
		{
			const float postNoiseInputAngleDiffDeg = RADIANS_TO_DEGREES(SafeAcos(Dot(m_prevInputAnglesLs.ToUnitVector(), inputGoalDirLs)));

			//MsgCon("post diff deg: %0.1f deg\n", postNoiseInputAngleDiffDeg);

			if (postNoiseInputAngleDiffDeg >= pDcDef->m_blinkAngleDiffDeg)
			{
				pBlinkController->Blink();
			}
		}
	}

	m_prevInputAnglesLs = inputAngles;

	const I32F selectedTri = FindNearestBlendTriangle(inputAngles.Theta(),
													  inputAngles.Phi(),
													  m_currentIslandIndex,
													  &m_goalAnglesLs);

	if (selectedTri >= 0)
	{
		const Vector constrainedGoalDirLs = m_goalAnglesLs.ToUnitVector();
		const Quat deltaRot = QuatFromVectors(inputGoalDirLs, constrainedGoalDirLs);

		m_constrainedTargetPosLs = Rotate(deltaRot, inputTargetPosLs - gestureOriginLs) + Point(kOrigin);
	}
	else
	{
		m_constrainedTargetPosLs = inputTargetPosLs;
		m_goalAnglesLs.FromThetaPhi(0.0f, 0.0f);
	}

	ANIM_ASSERT(IsReasonable(m_goalAnglesLs));

	if (m_firstFrame)
	{
		m_sprungAnglesLs	= m_goalAnglesLs;
		m_prevAnglesLs		= m_sprungAnglesLs;
		m_firstFrame = false;
	}
	else if (IsSingleAnimMode())
	{
		// stop updating sprung angles after first frame
	}
	else if (m_sprungAngleDelay > 0.0f)
	{
		m_sprungAngleDelay -= deltaTime;
	}
	else if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_disableSpringTracking))
	{
		m_sprungAnglesLs = m_goalAnglesLs;
		m_prevAnglesLs = m_sprungAnglesLs;
		m_hSpringLs.Reset();
		m_vSpringLs.Reset();
	}
	else
	{
		float angleDiffRad = 0.0f;

		if (m_pCacheData->m_wrap360)
		{
			angleDiffRad = SafeAcos(Dot(m_sprungAnglesLs.ToUnitVector(), inputGoalDirLs));
		}
		else
		{
			const Vec2 sprungVec = m_sprungAnglesLs.AsVec2();
			const Vec2 goalVec = SphericalCoords::FromVector(inputGoalDirLs).AsVec2();
			angleDiffRad = DEGREES_TO_RADIANS(Length(sprungVec - goalVec));
		}

		float k = LerpScale(0.0f, PI, m_springK, m_springKReduced, angleDiffRad);

		if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_springConstantOverride > 0.0f))
		{
			k = g_animOptions.m_gestures.m_springConstantOverride;
		}

		float dampingRatio = m_springDampingRatio;

		if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_springDampingRatioOverride > 0.0f))
		{
			dampingRatio = g_animOptions.m_gestures.m_springDampingRatioOverride;
		}

		if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_drawGestureDirMesh
								 && g_animOptions.IsGestureOrNoneSelected(m_resolvedGestureId)))
		{
			const Point drawPosOs = m_gestureOriginOs + m_gestureSpaceOs.TransformVector(m_sprungAnglesLs.ToUnitVector() * 0.5f);
			const Point drawPosWs = m_ownerLoc.GetLocatorWs().TransformPoint(drawPosOs);

			g_prim.Draw(DebugString(drawPosWs,
									StringBuilder<256>("spring k: %0.2f (angle diff: %0.1f deg)",
													   k,
													   RADIANS_TO_DEGREES(angleDiffRad))
										.c_str(),
									kColorWhiteTrans,
									0.5f));
		}

		m_prevAnglesLs = m_sprungAnglesLs;

		const float sprungThetaLs = m_hSpringLs.Track(m_sprungAnglesLs.Theta(),
													  m_goalAnglesLs.Theta(),
													  deltaTime,
													  k,
													  dampingRatio);

		const float sprungPhiLs = m_vSpringLs.Track(m_sprungAnglesLs.Phi(),
													m_goalAnglesLs.Phi(),
													deltaTime,
													k,
													dampingRatio);

		m_sprungAnglesLs = SphericalCoords::FromThetaPhi(sprungThetaLs, sprungPhiLs);
	}

	ANIM_ASSERT(IsReasonable(m_sprungAnglesLs));

	UpdateDesiredAlternative(pOwner, statePhase);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SphericalCoords IGestureNode::GetInputAngles(Vector_arg inputDirLs, bool* pInsideOut /* = nullptr */) const
{
	SphericalCoords bestCoords = SphericalCoords::FromVector(inputDirLs);
	if (m_firstFrame || !m_pCacheData->m_hyperRange)
	{
		return bestCoords;
	}

	const Vector prevDirLs = m_prevInputAnglesLs.ToUnitVector();

	const float deltaTheta = RADIANS_TO_DEGREES(GetRelativeXzAngleDiffRad(inputDirLs, prevDirLs));

	const float testTheta = m_prevInputAnglesLs.Theta() + deltaTheta;
	const float testPhi	  = bestCoords.Phi();

	SphericalCoords nearestAngles;
	bool inside = false;
	const I32F iTri = FindNearestBlendTriangle(testTheta, testPhi, m_currentIslandIndex, &nearestAngles, &inside);

	const bool hyperSelection = Abs(nearestAngles.Theta()) > 179.0f || Abs(nearestAngles.Phi()) > 89.0f;
	if (iTri >= 0 && inside && hyperSelection)
	{
		bestCoords = nearestAngles;
	}

	if (pInsideOut)
		*pInsideOut = inside;

	return bestCoords;
}

/// --------------------------------------------------------------------------------------------------------------- ///
FactDictionary* IGestureNode::AddGestureSpecificFacts(const FactDictionary* pFacts,
													  float statePhase,
													  const char* sourceStr) const
{
	const U32 kNumNewFacts = 6;
	FactDictionary* pNewDict = g_factMgr.CopyAndGrowFactDictionary(pFacts, kNumNewFacts);

	const float nodePhase = m_pCacheData->m_useDetachedPhase ? m_detachedPhase : statePhase;

	const float hInputAngleDeg = -m_prevInputAnglesLs.Theta();
	const float vInputAngleDeg = m_prevInputAnglesLs.Phi();

	const StringId64 altName = Gesture::GetAlternativeName(m_dcDef, m_cacheKey.m_altIndex);

	pNewDict->Add(SID("gesture-phase"), BoxedValue(nodePhase));
	pNewDict->Add(SID("gesture-mirror"), BoxedValue(m_cacheKey.m_flipped));
	pNewDict->Add(SID("gesture-alt-name"), BoxedValue(altName));
	pNewDict->Add(SID("h-goal-angle-deg"), BoxedValue(hInputAngleDeg));
	pNewDict->Add(SID("v-goal-angle-deg"), BoxedValue(vInputAngleDeg));

	if (FALSE_IN_FINAL_BUILD(g_dialogOptions.m_logFactDictMemory))
	{
		const char* memContextName = "[SCOPED-TEMP]";
		Memory::Allocator* pAlloc = Memory::TopAllocator();
		if (pAlloc != nullptr && pAlloc->GetName() != nullptr && pAlloc->GetName()[0] != '\0')
		{
			memContextName = pAlloc->GetName();
		}

		MsgLogFactDict("FACTDICT 0x%p <--grow-- 0x%p [%s] %s\n", pNewDict, pFacts, memContextName, sourceStr);
	}

	return pNewDict;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::UpdateDesiredAlternative(NdGameObject* pOwner, float statePhase)
{
	const DC::GestureDef* pDcDef = GetDcDef();

	if (!pDcDef)
	{
		m_requestedAltAnims = Gesture::kAlternativeIndexNone;
		m_requestedAltNoise = Gesture::kAlternativeIndexNone;
		return;
	}

	if (!pDcDef->m_gestureAlternatives && !pDcDef->m_offsetNoiseAlternatives)
	{
		m_requestedAltAnims = Gesture::kAlternativeIndexNone;
		m_requestedAltNoise = Gesture::kAlternativeIndexNone;
		return;
	}

	const float hInputAngleDeg = -m_prevInputAnglesLs.Theta();
	const float vInputAngleDeg = m_prevInputAnglesLs.Phi();

	//MsgCon("h: %0.1f\n", hInputAngleDeg);
	//MsgCon("v: %0.1f\n", vInputAngleDeg);

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);
	const FactDictionary* pFacts = pOwner ? pOwner->UpdateAndGetFactDict() : nullptr;
	FactMap::IDebugPrint* pDebugger = nullptr;

	FactDictionary* pNewDict = AddGestureSpecificFacts(pFacts, statePhase, "UpdateDesiredAlternative");

	const bool debug = FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_debugGestureAlternatives
											&& g_animOptions.IsGestureOrNoneSelected(m_resolvedGestureId)
											&& DebugSelection::Get().IsProcessOrNoneSelected(pOwner));

	if (pDcDef->m_gestureAlternatives)
	{
		bool shouldUpdate = true;

		const DC::GestureAlternative* pCurrentAlternative = Gesture::GetGestureAlternative(pDcDef, m_cacheKey.m_altIndex);

		if (pCurrentAlternative && pCurrentAlternative->m_sticky)
		{
			const DC::FactMap* pDcFactMap = pDcDef->m_gestureAlternatives;
			const DC::FactCriteriaDef* pCriteria = pDcFactMap->m_criteriaList[m_cacheKey.m_altIndex].m_ptr;

			if (g_factMgr.TestCriteria(*pCriteria, pNewDict))
			{
				m_requestedAltAnims = m_cacheKey.m_altIndex;

				if (FALSE_IN_FINAL_BUILD(debug))
				{
					pDebugger = NDI_NEW(kAllocSingleFrame) FactMap::DebugPrint<DC::GestureAlternative>(kMsgConPauseable);
					MsgConPauseable("Gesture Alternatives for %s (sticky):\n", DevKitOnly_StringIdToString(pDcDef->m_name));

					FactManager::CriteriaDebugInfo* pDebugInfos = NDI_NEW FactManager::CriteriaDebugInfo[pDcFactMap->m_numEntries];

					// just to fill in pDebugInfos
					g_factMgr.SelectBestCriteria(pDcFactMap->m_numEntries,
												 pDcFactMap->m_criteriaList,
												 pFacts,
												 nullptr,
												 pDebugInfos,
												 false);

					DebugFactMap(pDcFactMap, pDebugger, pDebugInfos, m_requestedAltAnims);
				}

				shouldUpdate = false;
			}
		}

		if (shouldUpdate)
		{
			const DC::GestureAlternative* pChosenAlternative = nullptr;
			m_requestedAltAnims = Gesture::DesiredAlternative(pDcDef, pNewDict, pChosenAlternative, debug);
		}
	}

	if (pDcDef->m_offsetNoiseAlternatives)
	{
		m_requestedAltNoise = Gesture::DesiredNoiseAlternative(pDcDef, pNewDict, debug);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IGestureNode::IsAlternateOutOfDate(const DC::GestureAlternative*& pChosenAlternative) const
{
	pChosenAlternative = nullptr;

	if (m_requestedAltAnims == Gesture::kAlternativeIndexUnspecified)
	{
		return false;
	}

	pChosenAlternative = Gesture::GetGestureAlternative(m_dcDef, m_requestedAltAnims);

	return m_requestedAltAnims != m_cacheKey.m_altIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point IGestureNode::GetSprungTargetPosWs() const
{
	const Vector targetDirOs = m_gestureSpaceOs.TransformVector(m_sprungAnglesLs.ToUnitVector() * 100.0f);
	const Point targetPosOs = m_gestureOriginOs + targetDirOs;
	const Point targetPosWs = m_ownerLoc.GetLocatorWs().TransformPoint(targetPosOs);
	return targetPosWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 IGestureNode::GetYawDeltaForFrame() const
{
	F32 angle = m_goalAnglesLs.Theta() - m_prevAnglesLs.Theta();
	return angle;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point IGestureNode::GetGoalTargetPosWs() const
{
	const Vector targetDirOs = m_gestureSpaceOs.TransformVector(m_goalAnglesLs.ToUnitVector() * 100.0f);
	const Point targetPosOs = m_gestureOriginOs + targetDirOs;
	const Point targetPosWs = m_ownerLoc.GetLocatorWs().TransformPoint(targetPosOs);
	return targetPosWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::GenerateAnimCommands_Internal(const AnimStateSnapshot* pSnapshot,
												 AnimCmdList* pAnimCmdList,
												 I32F outputInstance,
												 const AnimCmdGenInfo* pCmdGenInfo,
												 bool additive) const
{
	if (m_islandTransition.m_transitioning)
	{
		SelectedAnimInfo animInfoFrom, animInfoTo;

		SelectGestureAnims(&animInfoFrom, m_currentIslandIndex);
		SelectGestureAnims(&animInfoTo, m_islandTransition.m_targetIsland);

		GenerateAnimCommandsForSelection(pAnimCmdList, outputInstance + 0, pCmdGenInfo, animInfoFrom, additive);
		GenerateAnimCommandsForSelection(pAnimCmdList, outputInstance + 1, pCmdGenInfo, animInfoTo, additive);

		pAnimCmdList->AddCmd_EvaluateBlend(outputInstance + 0,
										   outputInstance + 1,
										   outputInstance,
										   ndanim::kBlendSlerp,
										   m_islandTransition.TransitionParameter());
	}
	else
	{
		SelectedAnimInfo animInfo;

		SelectGestureAnims(&animInfo, m_currentIslandIndex);

		GenerateAnimCommandsForSelection(pAnimCmdList, outputInstance, pCmdGenInfo, animInfo, additive);
	}

	if (GestureAnimsMirrored())
	{
		pAnimCmdList->AddCmd_EvaluateFlip(outputInstance);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void IGestureNode::AnimPluginCallback(StringId64 pluginId, AnimPluginContext* pPluginContext, const void* pData)
{
	if ((pPluginContext->m_pGroupContext->m_pSegmentContext->m_iSegment != 0)
		|| (pPluginContext->m_pGroupContext->m_iProcessingGroup != 0))
	{
		return;
	}

	const GestureAnimPluginData* pPluginData = (const GestureAnimPluginData*)pData;
	IGestureNode* pGestureNode = pPluginData->m_pSourceNode;

	JointSet* pJointSet = pPluginContext->m_pContext->m_pPluginJointSet;
	const NdGameObject* pJointSetGo = pJointSet ? pJointSet->GetNdGameObject() : nullptr;
	if (!pJointSetGo)
		return;

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	ndanim::JointParams* pJointParamsLs	   = pPluginContext->GetJoints(pPluginData->m_sourceInstance);
	const OrbisAnim::ValidBits* pValidBits = pPluginContext->GetValidBits(pPluginData->m_sourceInstance);

	const ndanim::JointHierarchy* pHierarchy	= pPluginContext->m_pContext->m_pSkel->m_pAnimHierarchy;
	const ndanim::JointParams* pDefaultParamsLs = ndanim::GetDefaultJointPoseTable(pHierarchy);

	if (pJointSet->ReadFromJointParams(pJointParamsLs, 0, OrbisAnim::kJointGroupSize, 1.0f, pValidBits, pDefaultParamsLs))
	{
		/*
		if (g_animOptions.IsGestureOrNoneSelected(pGestureNode->m_resolvedGestureId))
		{
			pJointSet->DebugDrawJoints(true);
		}
		*/

		switch (pluginId.GetValue())
		{
		case SID_VAL("gesture-preanim"):
			pGestureNode->AnimPlugin_UpdateGestureSpace(pJointSet);
			break;

		case SID_VAL("gesture-postblend"):
			pGestureNode->AnimPlugin_GetFeedback(pJointSet);

			if (pGestureNode->AnimPlugin_ApplyFixupIk(pJointSetGo,
													  pJointSet,
													  pPluginContext,
													  pPluginData->m_sourceInstance))
			{
				pJointSet->WriteJointParamsBlend(1.0f, pJointParamsLs, 0, OrbisAnim::kJointGroupSize, false);
			}
			break;
		}

		pJointSet->DiscardJointCache();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::AnimPlugin_UpdateGestureSpace(JointSet* pJointSet)
{
	if (IsSingleAnimMode())
		return;

	ANIM_ASSERT(pJointSet);

	if (m_pCacheData->m_animSpace.m_jointId != INVALID_STRING_ID_64)
	{
		m_prevGestureSpaceOs = m_gestureSpaceOs;
		m_gestureSpaceOs	 = kIdentity;

		const I32F jointOffset = pJointSet->FindJointOffset(m_pCacheData->m_animSpace.m_jointId);

		if (jointOffset >= 0)
		{
			const Locator jointLocOs = pJointSet->GetJointLocOs(jointOffset);

			m_gestureSpaceOs = jointLocOs.TransformLocator(m_pCacheData->m_animSpace.m_offsetLs);
		}
	}

	if (m_pCacheData->m_originSpace.m_jointId != INVALID_STRING_ID_64)
	{
		m_prevGestureOriginOs = m_gestureOriginOs;
		m_gestureOriginOs	  = kOrigin;

		const I32F jointOffset = pJointSet->FindJointOffset(m_pCacheData->m_originSpace.m_jointId);

		if (jointOffset >= 0)
		{
			const Locator jointLocOs = pJointSet->GetJointLocOs(jointOffset);

			m_gestureOriginOs = jointLocOs.TransformLocator(m_pCacheData->m_originSpace.m_offsetLs).Pos();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::AnimPlugin_GetFeedback(JointSet* pJointSet)
{
	ANIM_ASSERT(m_pCacheData);

	const DC::GestureDef* pDcDef = GetDcDef();
	if (!pDcDef || pDcDef->m_disableFeedback)
	{
		return;
	}

	if (m_pCacheData->m_feedbackSpace.m_jointId == INVALID_STRING_ID_64)
	{
		return;
	}

	const I32F jointOffset = pJointSet->FindJointOffset(m_pCacheData->m_feedbackSpace.m_jointId);
	if (jointOffset < 0)
	{
		return;
	}

	const Locator jointLocOs = pJointSet->GetJointLocOs(jointOffset);

	const I32F originOffset = (pDcDef && pDcDef->m_feedbackOrigin)
								  ? pJointSet->FindJointOffset(pDcDef->m_feedbackOrigin->m_jointId)
								  : -1;

	if (originOffset >= 0)
	{
		const Locator offsetLs = pDcDef->m_feedbackOrigin->m_offset ? *pDcDef->m_feedbackOrigin->m_offset : Locator(kIdentity);
		const Locator originOs = pJointSet->GetJointLocOs(originOffset).TransformLocator(offsetLs);

		HandleFeedback(jointLocOs, &originOs);
	}
	else
	{
		HandleFeedback(jointLocOs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IGestureNode::AnimPlugin_ApplyFixupIk(const NdGameObject* pGo,
										   JointSet* pJointSet,
										   const AnimPluginContext* pPluginContext,
										   U32F instanceIndex)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_disableFixupIk))
		return false;

	const DC::GestureDef* pDcDef = GetDcDef();
	const DC::GestureFixupIkConfig* pConfig = pDcDef ? pDcDef->m_fixupIk : nullptr;

	if (!pConfig || !pJointSet)
		return false;

	const I32F iConstraint = pJointSet->FindJointOffset(pConfig->m_constraint.m_jointId);
	if (iConstraint < 0)
	{
		if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_debugDrawFixupIk))
		{
			MsgConPauseable("[%s] Joint '%s' is not in the plugin joint set (gesture: '%s')\n",
							pGo->GetName(),
							DevKitOnly_StringIdToString(pConfig->m_constraint.m_jointId),
							DevKitOnly_StringIdToString(m_resolvedGestureId));
		}
		return false;
	}

	const I32F iJoint0 = pJointSet->FindJointOffset(pConfig->m_joints[0]);
	const I32F iJoint1 = pJointSet->FindJointOffset(pConfig->m_joints[1]);
	const I32F iJoint2 = pJointSet->FindJointOffset(pConfig->m_joints[2]);

	if ((iJoint0 < 0) || (iJoint1 < 0) || (iJoint2 < 0))
	{
		if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_debugDrawFixupIk))
		{
			if (iJoint0 < 0)
			{
				MsgConPauseable("[%s] Joint '%s' is not in the plugin joint set (gesture: '%s')\n",
								pGo->GetName(),
								DevKitOnly_StringIdToString(pConfig->m_joints[0]),
								DevKitOnly_StringIdToString(m_resolvedGestureId));
			}
			if (iJoint1 < 0)
			{
				MsgConPauseable("[%s] Joint '%s' is not in the plugin joint set (gesture: '%s')\n",
								pGo->GetName(),
								DevKitOnly_StringIdToString(pConfig->m_joints[1]),
								DevKitOnly_StringIdToString(m_resolvedGestureId));
			}
			if (iJoint2 < 0)
			{
				MsgConPauseable("[%s] Joint '%s' is not in the plugin joint set (gesture: '%s')\n",
								pGo->GetName(),
								DevKitOnly_StringIdToString(pConfig->m_joints[2]),
								DevKitOnly_StringIdToString(m_resolvedGestureId));
			}
		}

		return false;
	}

	const FgAnimData* pAnimData = pGo->GetAnimData();
	const ArtItemSkeleton* pAnimSkel = pPluginContext->m_pContext->m_pSkel;
	const ArtItemSkeleton* pSkel = pAnimData->m_curSkelHandle.ToArtItem();

	const I32F iRetargetEnds[2] = { iConstraint, iJoint2 };

	if (pSkel != pAnimSkel)
	{
		pJointSet->RetargetJointSubsets(iRetargetEnds, 2, pAnimSkel, pSkel);
	}

	const Locator animConstraintOs = pJointSet->GetJointLocOs(iConstraint);
	const Locator ikTargetOs = animConstraintOs.TransformLocator(m_fixupTargetDeltaOs);

	TwoBoneIkParams params;
	params.m_objectSpace  = true;
	params.m_goalPos	  = ikTargetOs.Pos();
	params.m_finalGoalRot = ikTargetOs.Rot();
	params.m_tt = 1.0f;
	params.m_jointOffsets[0]  = iJoint0;
	params.m_jointOffsets[1]  = iJoint1;
	params.m_jointOffsets[2]  = iJoint2;
	params.m_pJointSet		  = pJointSet;
	params.m_stretchLimit	  = 0.975f;

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_debugDrawFixupIk))
	{
		const Point p0Ws = pJointSet->GetJointLocWs(iJoint0).Pos();
		const Point p1Ws = pJointSet->GetJointLocWs(iJoint1).Pos();
		const Locator loc2Ws = pJointSet->GetJointLocWs(iJoint2);

		g_prim.Draw(DebugCross(p0Ws, 0.1f, kColorYellowTrans, PrimAttrib(kPrimDisableDepthTest)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugString(p0Ws, "pre-ik:0", kColorWhiteTrans, 0.7f), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugCross(p1Ws, 0.1f, kColorYellowTrans, PrimAttrib(kPrimDisableDepthTest)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugString(p1Ws, "pre-ik:1", kColorWhiteTrans, 0.7f), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugCoordAxes(loc2Ws, 0.1f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugString(loc2Ws.Pos(), "pre-ik:2", kColorWhite, 0.5f), kPrimDuration1FramePauseable);

		const Locator locWs = m_ownerLoc.GetLocatorWs();

		const Locator curConstraintWs = pJointSet->GetJointLocWs(iConstraint);
		g_prim.Draw(DebugCoordAxes(curConstraintWs, 0.1f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugString(curConstraintWs.Pos(), "pre-ik:c", kColorWhiteTrans, 0.7f), kPrimDuration1FramePauseable);

		const Locator ikTargetWs = locWs.TransformLocator(ikTargetOs);
		g_prim.Draw(DebugCoordAxes(ikTargetWs, 0.1f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugString(ikTargetWs.Pos(), "pre-ik:target", kColorWhite, 0.7f), kPrimDuration1FramePauseable);
	}

	TwoBoneIkResults results;

	const bool success = SolveTwoBoneIK(params, results);

	if (FALSE_IN_FINAL_BUILD(false))
	{
		const Point p0Ws = pJointSet->GetJointLocWs(iJoint0).Pos();
		const Point p1Ws = pJointSet->GetJointLocWs(iJoint1).Pos();
		const Locator loc2Ws = pJointSet->GetJointLocWs(iJoint2);

		//g_prim.Draw(DebugCross(p0Ws, 0.1f, kColorYellow, PrimAttrib(kPrimDisableDepthTest)), kPrimDuration1FramePauseable);
		//g_prim.Draw(DebugString(p0Ws, "ik:0", kColorWhite, 0.7f), kPrimDuration1FramePauseable);
		//g_prim.Draw(DebugCross(p1Ws, 0.1f, kColorYellow, PrimAttrib(kPrimDisableDepthTest)), kPrimDuration1FramePauseable);
		//g_prim.Draw(DebugString(p1Ws, "ik:1", kColorWhite, 0.7f), kPrimDuration1FramePauseable);
		//g_prim.Draw(DebugCross(p2Ws, 0.1f, kColorYellow, PrimAttrib(kPrimDisableDepthTest)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugCoordAxes(loc2Ws, 0.3f, kPrimEnableHiddenLineAlpha));
		g_prim.Draw(DebugString(loc2Ws.Pos(), "post-ik:2", kColorWhite, 0.7f), kPrimDuration1FramePauseable);
	}

	if (pSkel != pAnimSkel)
	{
		pJointSet->RetargetJointSubsets(iRetargetEnds, 2, pSkel, pAnimSkel);
	}

	if (success)
	{
		OrbisAnim::ValidBits* pValidBits = pPluginContext->GetValidBits(instanceIndex);
		OrbisAnim::ValidBits* pValidBits2 = pPluginContext->GetBlendGroupJointValidBits(instanceIndex);

		pJointSet->WriteJointValidBits(iJoint2, 0, pValidBits);
		pJointSet->WriteJointValidBits(iJoint2, 0, pValidBits2);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::HandleFeedback(const Locator& jointLocOs, const Locator* pOriginOs /* = nullptr */)
{
	ANIM_ASSERT(m_pCacheData);
	ANIM_ASSERT(IsReasonable(jointLocOs));

	const Locator actualLocOs = jointLocOs.TransformLocator(m_pCacheData->m_feedbackSpace.m_offsetLs);
	const Locator actualLocLs = m_gestureSpaceOs.UntransformLocator(actualLocOs);

	Vector inputAimDirLs = Unrotate(m_noiseQuat, GetAngles().ToUnitVector());
	Vector actualAimLs = Unrotate(m_noiseQuat, GetLocalZ(actualLocLs.Rot()));

	ANIM_ASSERT(IsReasonable(inputAimDirLs));
	ANIM_ASSERT(IsReasonable(actualAimLs));

	Point originOs = m_gestureOriginOs;

	if (pOriginOs)
	{
		originOs = pOriginOs->Pos();

		const Point actualOriginLs = m_gestureSpaceOs.UntransformPoint(originOs);

		const Vector inputAimDirOs = m_gestureSpaceOs.TransformVector(inputAimDirLs);
		const float distToOrigin = Dist(m_targetPosPs, m_ownerLoc.GetLocatorPs().TransformPoint(m_gestureOriginOs));
		const Point inputAimPosOs = m_gestureOriginOs + m_gestureSpaceOs.TransformVector(inputAimDirLs * distToOrigin);
		
		//g_prim.Draw(DebugSphere(m_ownerLoc.GetLocatorWs().TransformPoint(m_gestureOriginOs), 0.05f, kColorMagenta, kPrimEnableHiddenLineAlpha));
		//g_prim.Draw(DebugSphere(m_ownerLoc.GetLocatorWs().TransformPoint(inputAimPosOs), 0.05f, kColorCyan));

		const Vector inputFromOriginOs = inputAimPosOs - originOs;
		
		const Vector inputFromOriginLs = m_gestureSpaceOs.UntransformVector(inputFromOriginOs);
		inputAimDirLs = Unrotate(m_noiseQuat, SafeNormalize(inputFromOriginLs, inputAimDirLs));
		actualAimLs = Unrotate(m_noiseQuat, SafeNormalize(actualLocLs.Pos() - actualOriginLs, actualAimLs));

		ANIM_ASSERT(IsReasonable(inputAimDirLs));
		ANIM_ASSERT(IsReasonable(actualAimLs));
	}

	const Quat rawError = QuatFromVectors(inputAimDirLs, actualAimLs);

	m_feedbackError = Slerp(kIdentity, rawError, m_feedbackEffect);

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_debugDrawFeedback
							 && g_animOptions.IsGestureOrNoneSelected(m_resolvedGestureId)))
	{
		Vec4 feedbackAxis;
		float feedbackAngle;
		rawError.GetAxisAndAngle(feedbackAxis, feedbackAngle);
		const float errDeg = RADIANS_TO_DEGREES(feedbackAngle);

		const Locator alignWs = m_ownerLoc.GetLocatorWs();
		const Vector actualDirWs = alignWs.TransformVector(m_gestureSpaceOs.TransformVector(actualAimLs));

		const Quat actualRotWs = QuatFromLookAt(actualDirWs, kUnitYAxis);
		const Locator actualLocWs = Locator(alignWs.TransformPoint(actualLocOs.Pos()), actualRotWs);
		const Vector inputDirOs = m_gestureSpaceOs.TransformVector(inputAimDirLs);
		const Vector inputDirWs = alignWs.TransformVector(inputDirOs);
		const Point centerWs = alignWs.TransformPoint(originOs);

		g_prim.Draw(DebugArrow(centerWs, actualDirWs, kColorBlueTrans, 0.5f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugString(centerWs + actualDirWs, "Actual", kColorBlueTrans, 0.5f), kPrimDuration1FramePauseable);

		if (pOriginOs)
		{
			const Locator originWs = alignWs.TransformLocator(*pOriginOs);
			g_prim.Draw(DebugCoordAxes(originWs, 0.2f, kPrimEnableHiddenLineAlpha, 4.0f), kPrimDuration1FramePauseable);
		}

		g_prim.Draw(DebugCoordAxes(actualLocWs, 0.2f, kPrimEnableHiddenLineAlpha, 4.0f), kPrimDuration1FramePauseable);

		const DC::GestureDef* pDcDef = GetDcDef();
		const float feedbackStrength = GetGestureFeedbackStrength(pDcDef);

		g_prim.Draw(DebugArrow(centerWs, inputDirWs, kColorOrangeTrans, 0.5f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugStringFmt(centerWs + (inputDirWs * 0.75f),
								   kColorOrange,
								   0.5f,
								   "Input ['%s']\nErr: %0.2f deg * %0.1f%% @ %0.1f Str",
								   DevKitOnly_StringIdToString(m_pCacheData->m_feedbackSpace.m_jointId),
								   errDeg,
								   m_feedbackEffect * 100.0f,
								   feedbackStrength),
					kPrimDuration1FramePauseable);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::GenerateAnimCommandsForSelection(AnimCmdList* pAnimCmdList,
													I32F outputInstance,
													const AnimCmdGenInfo* pCmdGenInfo,
													const SelectedAnimInfo& animInfo,
													bool additive) const
{
	const I32F stage1Instance = outputInstance;
	const bool stage1Valid	  = AddCmdForBlend(pAnimCmdList,
											   animInfo.m_anims[0],
											   animInfo.m_anims[1],
											   animInfo.m_blend.x,
											   stage1Instance,
											   pCmdGenInfo,
											   additive);

	const I32F stage2Instance = stage1Valid ? (stage1Instance + 1) : stage1Instance;
	const bool stage2Valid = GetAnimFromIndex(animInfo.m_anims[2], additive) != nullptr;

	if (stage2Valid)
	{
		AddCmdForAnimation(pAnimCmdList, animInfo.m_anims[2], stage2Instance, pCmdGenInfo, additive);
	}

	if (stage1Valid && stage2Valid)
	{
		pAnimCmdList->AddCmd_EvaluateBlend(stage1Instance,
										   stage2Instance,
										   outputInstance,
										   ndanim::kBlendSlerp,
										   animInfo.m_blend.y);
	}
	else if (stage1Valid)
	{
		if (stage1Instance != outputInstance)
		{
			pAnimCmdList->AddCmd_EvaluateCopy(stage1Instance, outputInstance);
		}
	}
	else if (stage2Valid)
	{
		if (stage2Instance != outputInstance)
		{
			pAnimCmdList->AddCmd_EvaluateCopy(stage2Instance, outputInstance);
		}
	}
	else
	{
		pAnimCmdList->AddCmd_EvaluateEmptyPose(outputInstance);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IGestureNode::ValidBitsDifferBetweenAnims() const
{
	if (m_resolvedGestureId == INVALID_STRING_ID_64 || !m_pCacheData || m_pCacheData->m_numGestureAnims == 0)
		return false;

	static const U32 kMaxGroups = 16;

	bool animEncountered = false;
	U32 numProcessingGroupsSoFar = 0;
	ndanim::ValidBits bitsSoFar[kMaxGroups];
	const bool wantAdd = IsAdditive();

	for (U32F i = 0; i < m_pCacheData->m_numGestureAnims; ++i)
	{
		const ArtItemAnim* pAnim = GetAnimFromIndex(i, wantAdd);

		if (!pAnim)
			continue;

		const U32 numProcessingGroups = ndanim::GetNumProcessingGroups(pAnim->m_pClipData);
		if (animEncountered && numProcessingGroups != numProcessingGroupsSoFar)
			return true;

		ndanim::ValidBits bits[kMaxGroups];
		for (U32 group = 0; group < Min(kMaxGroups, numProcessingGroups); ++group)
		{
			bits[group] = *(ndanim::GetValidBitsArray(pAnim->m_pClipData, group));
		}

		if (animEncountered)
		{
			for (U32 group = 0; group < Min(kMaxGroups, numProcessingGroups); ++group)
			{
				if (bits[group] != bitsSoFar[group])
				{
					return true;
				}
			}
		}

		animEncountered = true;
		numProcessingGroupsSoFar = numProcessingGroups;
		for (U32 group = 0; group < Min(kMaxGroups, numProcessingGroups); ++group)
		{
			bitsSoFar[group] = bits[group];
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IGestureNode::HasErrors(const AnimStateSnapshot* pSnapshot) const
{
	bool hasErrors = false;

	if (m_pCacheData)
	{
		bool wantAdd   = false;
		bool wantSlerp = false;

		WantAddOrSlerp(wantAdd, wantSlerp);

		for (U32F i = 0; i < m_pCacheData->m_numGestureAnims; ++i)
		{
			const ArtItemAnim* pSlerpAnim = GetAnimFromIndex(i, false);
			const ArtItemAnim* pAddAnim = GetAnimFromIndex(i, true);

			if (!pSlerpAnim && wantSlerp)
			{
				hasErrors = true;
				break;
			}

			if (!pAddAnim && wantAdd)
			{
				hasErrors = true;
				break;
			}
		}
	}
	else
	{
		hasErrors = true;
	}

	return hasErrors;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IGestureNode::HasLoopingAnimation(const AnimStateSnapshot* pSnapshot) const
{
	if (!m_pCacheData)
		return false;

	bool wantAdd   = false;
	bool wantSlerp = false;

	WantAddOrSlerp(wantAdd, wantSlerp);

	for (U32F i = 0; i < m_pCacheData->m_numGestureAnims; ++i)
	{
		if (m_pCacheData->m_cachedAnims[i].m_phase >= 0.0f) // fixed phase implies no looping
			continue;

		const ArtItemAnim* pSlerpAnim = wantSlerp ? GetAnimFromIndex(i, false) : nullptr;
		const ArtItemAnim* pAddAnim	  = wantAdd ? GetAnimFromIndex(i, true) : nullptr;

		if (pSlerpAnim && pSlerpAnim->m_flags & ArtItemAnim::kLooping)
		{
			return true;
		}

		if (pAddAnim && pAddAnim->m_flags & ArtItemAnim::kLooping)
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::GetTriggeredEffects(const AnimStateSnapshot* pSnapshot,
									   EffectUpdateStruct& effectParams,
									   float nodeBlend,
									   const AnimInstance* pInstance) const
{
	if (!m_pCacheData)
		return;

	const StringId64 stateNameId = pSnapshot->m_animState.m_name.m_symbol;

	bool wantAdd   = false;
	bool wantSlerp = false;

	WantAddOrSlerp(wantAdd, wantSlerp);

	for (U32F i = 0; i < m_pCacheData->m_numGestureAnims; ++i)
	{
		const ArtItemAnim* pSlerpAnim = wantSlerp ? GetAnimFromIndex(i, false) : nullptr;
		const ArtItemAnim* pAddAnim	  = wantAdd ? GetAnimFromIndex(i, true) : nullptr;

		if (pSlerpAnim)
		{
			AnimSnapshotNodeAnimation::AddTriggeredEffectsForAnim(pSlerpAnim,
																  effectParams,
																  nodeBlend,
																  pInstance,
																  stateNameId);
		}

		if (pAddAnim)
		{
			AnimSnapshotNodeAnimation::AddTriggeredEffectsForAnim(pAddAnim,
																  effectParams,
																  nodeBlend,
																  pInstance,
																  stateNameId);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
ArtItemAnimHandle IGestureNode::GetChosenSingleAnim() const
{
	if (!IsSingleAnimMode())
	{
		return ArtItemAnimHandle();
	}

	SelectedAnimInfo selInfo;
	SelectGestureAnims(&selInfo, m_currentIslandIndex);

	const I32F iAnim = selInfo.m_anims[0];

	if (iAnim < 0)
	{
		return ArtItemAnimHandle();
	}

	const GestureCache::CachedAnim& anim = m_pCacheData->m_cachedAnims[iAnim];
	ArtItemAnimHandle hAnim = anim.m_hSlerpAnim;

	if (hAnim.IsNull())
	{
		hAnim = anim.m_hAddAnim;
	}

	return hAnim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IGestureNode::EvaluateFloatChannel(const AnimStateSnapshot* pSnapshot,
										float* pOutChannelFloat,
										const SnapshotEvaluateParams& params) const
{
	if (!pOutChannelFloat || !m_pCacheData || FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_disableGestureNodes))
	{
		return false;
	}

	bool evaluated = true;
	const bool wantAdd = IsAdditive();

	if (m_islandTransition.m_transitioning)
	{
		float channelFrom = 0.0f;
		float channelTo = 0.0f;

		evaluated = evaluated
					&& EvaluateFloatChannelForIsland(&channelFrom,
													 params,
													 m_currentIslandIndex,
													 wantAdd);

		evaluated = evaluated
					&& EvaluateFloatChannelForIsland(&channelTo,
													 params,
													 m_islandTransition.m_targetIsland,
													 wantAdd);

		if (evaluated)
		{
			*pOutChannelFloat = Lerp(channelFrom, channelTo, m_islandTransition.TransitionParameter());
		}
	}
	else
	{
		evaluated = EvaluateFloatChannelForIsland(pOutChannelFloat,
												  params,
												  m_currentIslandIndex,
												  wantAdd);
	}

	return evaluated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IGestureNode::EvaluateFloatChannelForIsland(float* pOutChannelFloat,
												 const SnapshotEvaluateParams& params,
												 int islandIndex,
												 bool additive) const
{
	if (!pOutChannelFloat || !m_pCacheData || FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_disableGestureNodes))
	{
		return false;
	}

	*pOutChannelFloat = 0.0f;

	if ((m_resolvedGestureId == INVALID_STRING_ID_64) || (m_pCacheData->m_numGestureAnims <= 0))
	{
		return false;
	}

	SelectedAnimInfo animInfo;
	SelectGestureAnims(&animInfo, islandIndex);

	if (animInfo.m_anims[0] < 0)
	{
		return false;
	}

	const bool animsMirrored = GestureAnimsMirrored();
	const float nodePhase = m_pCacheData->m_useDetachedPhase ? m_detachedPhase : params.m_statePhase;

	EvaluateChannelParams evalParams;
	evalParams.m_channelNameId = params.m_channelName;
	evalParams.m_phase = nodePhase;
	evalParams.m_mirror = params.m_flipped ^ animsMirrored;
	evalParams.m_wantRawScale = params.m_wantRawScale;
	evalParams.m_pCameraCutInfo = params.m_pCameraCutInfo;

	const I32F animIndexA = animInfo.m_anims[0];
	const I32F animIndexB = animInfo.m_anims[1];
	const I32F animIndexC = animInfo.m_anims[2];

	const GestureCache::CachedAnim* pAnimA = (animIndexA >= 0) ? &m_pCacheData->m_cachedAnims[animIndexA] : nullptr;
	const GestureCache::CachedAnim* pAnimB = (animIndexB >= 0) ? &m_pCacheData->m_cachedAnims[animIndexB] : nullptr;
	const GestureCache::CachedAnim* pAnimC = (animIndexC >= 0) ? &m_pCacheData->m_cachedAnims[animIndexC] : nullptr;

	const ArtItemAnim* pArtItemAnimA = GetAnimFromIndex(animIndexA, additive);
	const ArtItemAnim* pArtItemAnimB = GetAnimFromIndex(animIndexB, additive);
	const ArtItemAnim* pArtItemAnimC = GetAnimFromIndex(animIndexC, additive);

	const float phaseA = (pAnimA && pAnimA->m_phase >= 0.0f) ? pAnimA->m_phase : nodePhase;
	const float phaseB = (pAnimB && pAnimB->m_phase >= 0.0f) ? pAnimB->m_phase : nodePhase;
	const float phaseC = (pAnimC && pAnimC->m_phase >= 0.0f) ? pAnimC->m_phase : nodePhase;

	float resA = 0.0f;
	evalParams.m_pAnim = pArtItemAnimA;
	evalParams.m_phase = phaseA;
	const bool aValid = pArtItemAnimA ? EvaluateCompressedFloatChannel(&evalParams, &resA) : false;

	float resB = 0.0f;
	evalParams.m_pAnim = pArtItemAnimB;
	evalParams.m_phase = phaseB;
	const bool bValid = pArtItemAnimB ? EvaluateCompressedFloatChannel(&evalParams, &resB) : false;

	float resC = 0.0f;
	evalParams.m_pAnim = pArtItemAnimC;
	evalParams.m_phase = phaseC;
	const bool cValid = pArtItemAnimC ? EvaluateCompressedFloatChannel(&evalParams, &resC) : false;

	float blendedRes = 0.0f;

	if (aValid)
	{
		// three-way blend
		blendedRes = resA;
	}

	if (bValid)
	{
		if (aValid)
		{
			blendedRes = Lerp(resA, resB, animInfo.m_blend.x);
		}
		else
		{
			blendedRes = resB;
		}
	}

	if (cValid)
	{
		if (aValid || bValid)
		{
			blendedRes = Lerp(blendedRes, resC, animInfo.m_blend.y);
		}
		else
		{
			blendedRes = resC;
		}
	}

	*pOutChannelFloat = blendedRes;

	const bool valid = aValid || bValid || cValid;

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IGestureNode::EvaluateChannel(const AnimStateSnapshot* pSnapshot,
								   ndanim::JointParams* pOutChannelJoint,
								   const SnapshotEvaluateParams& params) const
{
	if (!pOutChannelJoint || !m_pCacheData || FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_disableGestureNodes))
	{
		return false;
	}

	bool evaluated = true;

	if (m_islandTransition.m_transitioning)
	{
		ndanim::JointParams paramsFrom, paramsTo;

		evaluated = evaluated
					&& EvaluateChannelForIsland(pSnapshot,
												&paramsFrom,
												params,
												m_currentIslandIndex);

		evaluated = evaluated
					&& EvaluateChannelForIsland(pSnapshot,
												&paramsTo,
												params,
												m_islandTransition.m_targetIsland);

		if (evaluated)
		{
			AnimChannelJointBlend(pOutChannelJoint, paramsFrom, paramsTo, m_islandTransition.TransitionParameter());
		}
	}
	else
	{
		evaluated = EvaluateChannelForIsland(pSnapshot,
											 pOutChannelJoint,
											 params,
											 m_currentIslandIndex);
	}

	return evaluated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IGestureNode::EvaluateChannelForIsland(const AnimStateSnapshot* pSnapshot,
											ndanim::JointParams* pOutChannelJoint,
											const SnapshotEvaluateParams& params,
											int islandIndex) const
{
	if (!pOutChannelJoint || !m_pCacheData)
	{
		return false;
	}

	pOutChannelJoint->m_scale = Vector(Scalar(1.0f));
	pOutChannelJoint->m_quat = Quat(kIdentity);
	pOutChannelJoint->m_trans = Point(kOrigin);

	if ((m_resolvedGestureId == INVALID_STRING_ID_64) || (m_pCacheData->m_numGestureAnims <= 0))
	{
		return false;
	}

	SelectedAnimInfo animInfo;
	SelectGestureAnims(&animInfo, islandIndex);

	if (animInfo.m_anims[0] < 0)
	{
		return false;
	}

	const bool wantAdd = IsAdditive();

	const float nodePhase = m_pCacheData->m_useDetachedPhase ? m_detachedPhase : params.m_statePhase;
	const bool radialBlend = (pSnapshot->m_animState.m_flags & DC::kAnimStateFlagRadialChannelBlending);

	const bool animsMirrored = GestureAnimsMirrored();

	EvaluateChannelParams evalParams;
	evalParams.m_channelNameId = params.m_channelName;
	evalParams.m_phase		   = nodePhase;
	evalParams.m_mirror		   = params.m_flipped ^ animsMirrored;
	evalParams.m_wantRawScale  = params.m_wantRawScale;
	evalParams.m_pCameraCutInfo = params.m_pCameraCutInfo;

	const I32F animIndexA = animInfo.m_anims[0];
	const I32F animIndexB = animInfo.m_anims[1];
	const I32F animIndexC = animInfo.m_anims[2];

	const GestureCache::CachedAnim* pAnimA = (animIndexA >= 0) ? &m_pCacheData->m_cachedAnims[animIndexA] : nullptr;
	const GestureCache::CachedAnim* pAnimB = (animIndexB >= 0) ? &m_pCacheData->m_cachedAnims[animIndexB] : nullptr;
	const GestureCache::CachedAnim* pAnimC = (animIndexC >= 0) ? &m_pCacheData->m_cachedAnims[animIndexC] : nullptr;

	const ArtItemAnim* pArtItemAnimA = GetAnimFromIndex(animIndexA, wantAdd);
	const ArtItemAnim* pArtItemAnimB = GetAnimFromIndex(animIndexB, wantAdd);
	const ArtItemAnim* pArtItemAnimC = GetAnimFromIndex(animIndexC, wantAdd);

	const float phaseA = (pAnimA && (pAnimA->m_phase >= 0.0f)) ? pAnimA->m_phase : nodePhase;
	const float phaseB = (pAnimB && (pAnimB->m_phase >= 0.0f)) ? pAnimB->m_phase : nodePhase;
	const float phaseC = (pAnimC && (pAnimC->m_phase >= 0.0f)) ? pAnimC->m_phase : nodePhase;

	ndanim::JointParams resA;
	evalParams.m_pAnim = pArtItemAnimA;
	evalParams.m_phase = phaseA;
	const bool aValid = pArtItemAnimA ? EvaluateChannelInAnim(m_cacheKey.m_skelId, &evalParams, &resA) : false;

	ndanim::JointParams resB;
	evalParams.m_pAnim = pArtItemAnimB;
	evalParams.m_phase = phaseB;
	const bool bValid = pArtItemAnimB ? EvaluateChannelInAnim(m_cacheKey.m_skelId, &evalParams, &resB) : false;

	ndanim::JointParams resC;
	evalParams.m_pAnim = pArtItemAnimC;
	evalParams.m_phase = phaseC;
	const bool cValid = pArtItemAnimC ? EvaluateChannelInAnim(m_cacheKey.m_skelId, &evalParams, &resC) : false;

	ndanim::JointParams blendedRes;

	if (aValid)
	{
		blendedRes = resA;
	}

	if (bValid)
	{
		if (aValid)
		{
			BlendChannelJoint(&blendedRes, resA, resB, animInfo.m_blend.x, radialBlend);
		}
		else
		{
			blendedRes = resB;
		}
	}

	if (cValid)
	{
		if (aValid || bValid)
		{
			ndanim::JointParams tmp;

			BlendChannelJoint(&tmp, blendedRes, resC, animInfo.m_blend.y, radialBlend);

			blendedRes = tmp;
		}
		else
		{
			blendedRes = resC;
		}
	}

	*pOutChannelJoint = blendedRes;

	const bool valid = aValid || bValid || cValid;

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::CollectContributingAnims(const AnimStateSnapshot* pSnapshot,
											float blend,
											AnimCollection* pCollection) const
{
	if (!m_pCacheData)
		return;

	if (blend <= 0.001f)
		return;

	if ((m_resolvedGestureId == INVALID_STRING_ID_64) || (m_pCacheData->m_numGestureAnims <= 0))
		return;

	SelectedAnimInfo animInfo;
	SelectGestureAnims(&animInfo, m_currentIslandIndex);

	bool wantAdd = false;
	bool wantSlerp = false;

	WantAddOrSlerp(wantAdd, wantSlerp);

	for (U32F i = 0; i < ARRAY_COUNT(animInfo.m_anims); ++i)
	{
		const I32F index = animInfo.m_anims[i];

		if (index < 0)
			continue;

		if (pCollection->m_animCount >= AnimIdCollection::kMaxAnimIds)
			break;

		const ArtItemAnim* pAddAnim = m_pCacheData->m_cachedAnims[index].m_hAddAnim.ToArtItem();
		const ArtItemAnim* pSlerpAnim = m_pCacheData->m_cachedAnims[index].m_hSlerpAnim.ToArtItem();

		if (wantAdd && pAddAnim)
		{
			pCollection->m_animArray[pCollection->m_animCount] = pAddAnim;
			++pCollection->m_animCount;
		}

		if (pCollection->m_animCount >= AnimIdCollection::kMaxAnimIds)
			break;

		if (wantSlerp && pSlerpAnim)
		{
			pCollection->m_animArray[pCollection->m_animCount] = pSlerpAnim;
			++pCollection->m_animCount;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const
{
	Tab(pData->m_output, pData->m_depth);

	Color outputColor = Lerp(kColorDarkGrayTrans, kColorOrange, pData->m_parentBlendValue);

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_disableGestureNodes))
	{
		outputColor = kColorDarkGrayTrans;
	}

	SetColor(pData->m_output, outputColor);

	if (m_loddedOut)
	{
		if (const ArtItemAnim* pLowLodAnim = m_pCacheData->m_hLowLodAnim.ToArtItem())
		{
			PrintTo(pData->m_output,
					"Gesture: %s",
					DevKitOnly_StringIdToString(m_resolvedGestureId));

			SetColor(pData->m_output, kColorCyan);
			PrintTo(pData->m_output, " [Low Lod: %s]\n", pLowLodAnim->GetName());
		}
		else
		{
			SetColor(pData->m_output, kColorYellow);

			PrintTo(pData->m_output, "Gesture: %s Lodded out.\n", DevKitOnly_StringIdToString(m_resolvedGestureId));
		}

		SetColor(pData->m_output, kColorWhite);

		return;
	}

	if (!m_pCacheData)
	{
		SetColor(pData->m_output, kColorRed);

		PrintTo(pData->m_output, "Gesture: %s CACHE FAILED\n", DevKitOnly_StringIdToString(m_resolvedGestureId));

		SetColor(pData->m_output, kColorWhite);

		return;
	}

	bool wantAdd = false;
	bool wantSlerp = false;

	WantAddOrSlerp(wantAdd, wantSlerp);

	SelectedAnimInfo animInfo;

	SelectGestureAnims(&animInfo, m_currentIslandIndex);

	float sampleFrame = -1.0f;
	float totalFrames = -1.0f;

	{
		static const U32 kSelectedAnimCount = ARRAY_COUNT(animInfo.m_anims);

		float sampleFrameArray[kSelectedAnimCount];

		for (U32 i = 0; i < kSelectedAnimCount; ++i)
		{
			if (animInfo.m_anims[i] >= 0)
			{
				sampleFrameArray[i] = GetSampleFrame(m_pCacheData->m_cachedAnims[animInfo.m_anims[i]],
													 wantAdd,
													 pData->m_statePhase,
													 &totalFrames);
			}
			else
			{
				sampleFrameArray[i] = -1.0f;
			}
		}

		if (sampleFrameArray[0] >= 0.0f && ValuesNearlyEqual(sampleFrameArray, kSelectedAnimCount))
		{
			sampleFrame = sampleFrameArray[0];
		}
	}

	PrintTo(pData->m_output, "Gesture: %s ", DevKitOnly_StringIdToString(m_resolvedGestureId));

	if ((m_inputGestureId != INVALID_STRING_ID_64) && m_inputGestureId != m_resolvedGestureId)
	{
		SetColor(pData->m_output, 0xFF999999);

		PrintTo(pData->m_output, "[%s] ", DevKitOnly_StringIdToString(m_inputGestureId));

		SetColor(pData->m_output, outputColor);
	}

	if (m_pCacheData->m_hasDuplicateBlendDir)
	{
		SetColor(pData->m_output, kColorRed);

		PrintTo(pData->m_output, "(Warning: Multiple frames with same gesture dir) ");

		SetColor(pData->m_output, outputColor);
	}

	StringBuilder<256> typeStr;
	typeStr.format(GetGestureAnimTypeStr(m_pCacheData->m_key.m_gestureNodeType));

	if (m_cacheKey.m_flipped)
	{
		typeStr.append("-flipped");
	}

	if (m_useLegFixIk)
	{
		typeStr.append("-legfix");
	}

	if (m_handFixIkMask != 0)
	{
		typeStr.append("-handfix-");
		DC::GetHandIkHandString(m_handFixIkMask, &typeStr, "-");
	}

	if (m_dcDef->m_weaponIkFeather)
	{
		typeStr.append("-wik-feather");
	}

	if (m_pCacheData->m_isBad)
	{
		PrintTo(pData->m_output, "(%s) <INVALID>", typeStr.c_str());
	}
	else
	{
		PrintTo(pData->m_output,
				"(%s) [%0.1fdeg / %0.1fdeg] [goal: %0.1fdeg /%0.1fdeg]",
				typeStr.c_str(),
				m_sprungAnglesLs.Theta(),
				m_sprungAnglesLs.Phi(),
				m_goalAnglesLs.Theta(),
				m_goalAnglesLs.Phi());
	}

	if (sampleFrame >= 0.0f)
	{
		PrintTo(pData->m_output, " frame: %0.2f/%0.2f", sampleFrame, totalFrames);
	}

	if (m_pCacheData->m_useDetachedPhase)
	{
		PrintTo(pData->m_output, " [detached]");
	}

	if ((m_feedbackEffect > 0.0f) && !m_dcDef->m_disableFeedback)
	{
		Vec4 axis;
		float errorRad;
		float filteredRad;
		m_feedbackError.GetAxisAndAngle(axis, errorRad);
		m_filteredFeedback.GetAxisAndAngle(axis, filteredRad);

		if (errorRad > 0.001f || filteredRad > 0.091f)
		{
			PrintTo(pData->m_output, " [feedback: %0.1fdeg, des: %0.1fdeg]", RADIANS_TO_DEGREES(filteredRad), RADIANS_TO_DEGREES(errorRad));
		}
	}

	PrintTo(pData->m_output, "\n");

	if (g_animOptions.m_debugPrint.m_simplified)
	{
		return;
	}

	if (m_pCacheData->m_isBad)
	{
		Maybe<StringId64> missingSlerp;
		Maybe<StringId64> missingAdd;

		FindFirstMissingAnims(missingSlerp, missingAdd);

		if (missingSlerp.Valid())
		{
			StringId64 missingAnimId = missingSlerp.Get();

			if (missingAnimId != INVALID_STRING_ID_64)
			{
				SetColor(pData->m_output, kColorRed);
				Tab(pData->m_output, pData->m_depth + 1);
				PrintTo(pData->m_output, "Slerp anim '%s' MISSING\n", DevKitOnly_StringIdToString(missingAnimId));
			}
			else
			{
				SetColor(pData->m_output, kColorRed);
				Tab(pData->m_output, pData->m_depth + 1);
				PrintTo(pData->m_output, "Slerp anim required but not provided\n");
			}
		}

		if (missingAdd.Valid())
		{
			StringId64 missingAnimId = missingAdd.Get();

			if (missingAnimId != INVALID_STRING_ID_64)
			{
				SetColor(pData->m_output, kColorRed);
				Tab(pData->m_output, pData->m_depth + 1);
				PrintTo(pData->m_output, "Additive anim '%s' MISSING\n", DevKitOnly_StringIdToString(missingAnimId));
			}
			else
			{
				SetColor(pData->m_output, kColorRed);
				Tab(pData->m_output, pData->m_depth + 1);
				PrintTo(pData->m_output, "Additive anim required but not provided\n");
			}
		}

		if (!missingSlerp.Valid() && !missingAdd.Valid())
		{
			SetColor(pData->m_output, kColorRed);
			Tab(pData->m_output, pData->m_depth + 1);
			PrintTo(pData->m_output, "Cache data is bad but we don't know why\n");
		}
	}
	else
	{
		if (wantAdd)
		{
			DebugPrintSelectedAnims(pData, animInfo, outputColor, true);
		}
		if (wantSlerp)
		{
			DebugPrintSelectedAnims(pData, animInfo, outputColor, false);
		}
	}

	if (ValidBitsDifferBetweenAnims())
	{
		SetColor(pData->m_output, kColorRed);
		Tab(pData->m_output, pData->m_depth + 1);
		PrintTo(pData->m_output, "Warning: Valid bits differ between gesture node anims -- double check in Builder\n");
	}

	SetColor(pData->m_output, kColorWhite);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::DebugPrintSelectedAnims(AnimNodeDebugPrintData* pData,
										   const SelectedAnimInfo& animInfo,
										   const Color outputColor,
										   bool wantAdd) const
{
	STRIP_IN_FINAL_BUILD;

	const I32F iA = animInfo.m_anims[0];
	const I32F iB = animInfo.m_anims[1];
	const I32F iC = animInfo.m_anims[2];

	const StringId64 baseNameIdA = wantAdd && (iA >= 0) ? m_pCacheData->m_cachedAnims[iA].m_addBaseName
														: m_pCacheData->m_cachedAnims[iA].m_slerpBaseName;
	const StringId64 baseNameIdB = wantAdd && (iB >= 0) ? m_pCacheData->m_cachedAnims[iB].m_addBaseName
														: m_pCacheData->m_cachedAnims[iB].m_slerpBaseName;
	const StringId64 baseNameIdC = wantAdd && (iC >= 0) ? m_pCacheData->m_cachedAnims[iC].m_addBaseName
														: m_pCacheData->m_cachedAnims[iC].m_slerpBaseName;

	const ArtItemAnim* pAnimA = GetAnimFromIndex(iA, wantAdd);
	const ArtItemAnim* pAnimB = GetAnimFromIndex(iB, wantAdd);
	const ArtItemAnim* pAnimC = GetAnimFromIndex(iC, wantAdd);

	if (pData->m_pRemapLayer)
	{
		pAnimA = pData->m_pRemapLayer->GetRemappedArtItem(pAnimA, 0);
		pAnimB = pData->m_pRemapLayer->GetRemappedArtItem(pAnimB, 0);
		pAnimC = pData->m_pRemapLayer->GetRemappedArtItem(pAnimC, 0);
	}

	const float frameA = iA >= 0 ? m_pCacheData->m_cachedAnims[iA].m_frame : -1.0f;
	const float frameB = iB >= 0 ? m_pCacheData->m_cachedAnims[iB].m_frame : -1.0f;
	const float frameC = iC >= 0 ? m_pCacheData->m_cachedAnims[iC].m_frame : -1.0f;

	const I32F baseDepth = pData->m_depth + 1;

	if (iA >= 0)
	{
		Tab(pData->m_output, baseDepth);
		PrintTo(pData->m_output, "[");

		if (pAnimA)
		{
			PrintTo(pData->m_output, pAnimA->GetName());
		}
		else
		{
			SetColor(pData->m_output, kColorRed);
			PrintTo(pData->m_output, "%s (MISSING)", DevKitOnly_StringIdToString(baseNameIdA));
			SetColor(pData->m_output, outputColor);
		}

		if (frameA >= 0.0f)
		{
			PrintTo(pData->m_output, StringBuilder<256>(" (%0.1f)", frameA).c_str());
		}

		if (iB >= 0)
		{
			PrintTo(pData->m_output, " %.2f ", animInfo.m_blend.x);

			if (pAnimB)
			{
				PrintTo(pData->m_output, pAnimB->GetName());
			}
			else
			{
				SetColor(pData->m_output, kColorRed);
				PrintTo(pData->m_output, "%s (MISSING)", DevKitOnly_StringIdToString(baseNameIdB));
				SetColor(pData->m_output, outputColor);
			}

			if (frameB >= 0.0f)
			{
				PrintTo(pData->m_output, StringBuilder<256>(" (%0.1f)", frameB).c_str());
			}
		}

		PrintTo(pData->m_output, "]");

		if (iC >= 0)
		{
			PrintTo(pData->m_output, " %.2f ", animInfo.m_blend.y);

			if (pAnimC)
			{
				PrintTo(pData->m_output, pAnimC->GetName());
			}
			else
			{
				SetColor(pData->m_output, kColorRed);
				PrintTo(pData->m_output, "%s (MISSING)", DevKitOnly_StringIdToString(baseNameIdC));
				SetColor(pData->m_output, outputColor);
			}

			if (frameC >= 0.0f)
			{
				PrintTo(pData->m_output, StringBuilder<256>(" (%0.1f)", frameC).c_str());
			}
		}

		PrintTo(pData->m_output, "\n");
	}
	else
	{
		SetColor(pData->m_output, kColorRed);
		Tab(pData->m_output, pData->m_depth + 1);
		PrintTo(pData->m_output, "No valid %s anims\n", wantAdd ? "additive" : "slerp");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::FindFirstMissingAnims(Maybe<StringId64>& missingSlerp, Maybe<StringId64>& missingAdd) const
{
	missingSlerp = MAYBE::kNothing;
	missingAdd = MAYBE::kNothing;

	const DC::GestureAnims* pDcAnims = GetDcAnimsDef();
	if (!pDcAnims)
		return;

	bool wantSlerp = false;
	bool wantAdd = false;

	switch (m_cacheKey.m_gestureNodeType)
	{
	case Gesture::AnimType::kAdditive:
		wantAdd = true;
		wantSlerp = false;
		break;

	case Gesture::AnimType::kSlerp:
		wantAdd = false;
		wantSlerp = true;
		break;

	case Gesture::AnimType::kCombo:
		wantAdd = true;
		wantSlerp = true;
		break;
	default:
		ANIM_ASSERTF(false, ("Unknown gesture node type '%d'", m_cacheKey.m_gestureNodeType));
		break;
	}

	const SkeletonId skelId = m_cacheKey.m_skelId;
	const U32 hierarchyId = m_pCacheData->m_hierarchyId;

	for (U32F inputPair = 0; inputPair < pDcAnims->m_numAnimPairs; ++inputPair)
	{
		const DC::GestureAnimPair& animPair = pDcAnims->m_animPairs[inputPair];

		if (wantAdd && !missingAdd.Valid()
			&& !AnimMasterTable::LookupAnim(skelId, hierarchyId, animPair.m_additiveAnim).ToArtItem())
		{
			missingAdd = animPair.m_additiveAnim;
		}

		if (wantSlerp && !missingSlerp.Valid()
			&& !AnimMasterTable::LookupAnim(skelId, hierarchyId, animPair.m_partialAnim).ToArtItem())
		{
			missingSlerp = animPair.m_partialAnim;
		}

		if (missingSlerp.Valid() == wantSlerp && missingAdd.Valid() == wantAdd)
			break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::DebugSubmitAnimPlayCount(const AnimStateSnapshot* pSnapshot) const
{
	STRIP_IN_FINAL_BUILD;

	if (!m_pCacheData)
		return;

	for (U32F i = 0; i < m_pCacheData->m_numGestureAnims; ++i)
	{
		if (const ArtItemAnim* pSlerpAnim = m_pCacheData->m_cachedAnims[i].m_hSlerpAnim.ToArtItem())
		{
			g_animStat.SubmitPlayCount(pSlerpAnim->m_skelID, pSlerpAnim->GetNameId(), 1);
		}

		if (const ArtItemAnim* pAddAnim = m_pCacheData->m_cachedAnims[i].m_hAddAnim.ToArtItem())
		{
			g_animStat.SubmitPlayCount(pAddAnim->m_skelID, pAddAnim->GetNameId(), 1);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::DebugDraw(const NdGameObject* pOwner) const
{
	STRIP_IN_FINAL_BUILD;

	const DC::GestureDef* pDcGesture = GetDcDef();

	if (!m_pCacheData || !pDcGesture || !g_animOptions.IsGestureOrNoneSelected(m_resolvedGestureId) || m_loddedOut)
	{
		return;
	}

	if (!g_animOptions.m_gestures.m_drawGestureDirMesh)
	{
		return;
	}

	const float drawRadius = 1.0f;

	bool wantAdd = false;
	bool wantSlerp = false;

	WantAddOrSlerp(wantAdd, wantSlerp);

	const bool didAlreadyDraw = g_gestureNodeDebugDrawManager.DidAlreadyDraw(pOwner, m_resolvedGestureId);

	StringId64 err = INVALID_STRING_ID_64;

	const Locator alignWs = m_ownerLoc.GetLocatorWs();
	const Locator gestureSpaceWs = alignWs.TransformLocator(m_gestureSpaceOs);
	const Point centerLs = m_gestureSpaceOs.UntransformPoint(m_gestureOriginOs);
	PrimServerWrapper ps = PrimServerWrapper(gestureSpaceWs);

	const Point centerWs = gestureSpaceWs.TransformPoint(centerLs);

	if (m_targetValid)
	{
		const Point targetPosWs = pOwner->GetParentSpace().TransformPoint(m_targetPosPs);
		const Vector targetDirWs = SafeNormalize(targetPosWs - centerWs, kUnitZAxis);

		g_prim.Draw(DebugString(centerWs + (targetDirWs * drawRadius * 2.0f), StringBuilder<256>("[%0.3f, %0.3f]", m_prevInputAnglesLs.Theta(), m_prevInputAnglesLs.Phi()).c_str(), kColorYellowTrans, 0.4f));
		g_prim.Draw(DebugArrow(centerWs,
							   targetPosWs,
							   kColorYellowTrans,
							   0.5f,
							   kPrimEnableHiddenLineAlpha));
	}

	SphericalCoords proxyComp = SphericalCoords::FromThetaPhi(0.0f, 0.0f);
	Vector proxyDirLs = kUnitZAxis;

	// if (m_targetValid)
	{
		proxyDirLs = GetLocalZ(m_proxyRotLs);
		proxyComp = SphericalCoords::FromVector(proxyDirLs);
	}

	const float hSprungDeg = m_sprungAnglesLs.Theta() - proxyComp.Theta();
	const float vSprungDeg = m_sprungAnglesLs.Phi() - proxyComp.Phi();

	const float hGoalDeg = m_goalAnglesLs.Theta() - proxyComp.Theta();
	const float vGoalDeg = m_goalAnglesLs.Phi() - proxyComp.Phi();

	SphericalCoords compedGoal;
	FindNearestBlendTriangle(hGoalDeg, vGoalDeg, m_currentIslandIndex, &compedGoal);

	SphericalCoords closestAngles = m_sprungAnglesLs;
	const I32F selectedTri = FindNearestBlendTriangle(hSprungDeg, vSprungDeg, m_currentIslandIndex, &closestAngles);

	const Vector sprungDirLs = closestAngles.ToUnitVector();
	const Vector goalDirLs = compedGoal.ToUnitVector();

	ps.DisableDepthTest();
	ps.DrawCross(centerLs + (goalDirLs * drawRadius), 0.1f, kColorRedTrans);
	ps.DrawArrow(centerLs, sprungDirLs * drawRadius * 1.1f, 0.25f, kColorRedTrans);

	if (m_sprungAngleDelay >= 0.0f)
	{
		ps.DrawString(centerLs + (sprungDirLs * drawRadius * 1.1f), StringBuilder<128>("DELAYED %0.1fsec", m_sprungAngleDelay).c_str(), kColorRed, 0.4f);
	}

	if (Abs(m_hSpringLs.m_speed) > 0.0f || Abs(m_vSpringLs.m_speed) > 0.0f)
	{
		const float kLookAheadSec = 0.2f;
		SphericalCoords speedCoords = closestAngles;
		speedCoords.Adjust(m_hSpringLs.m_speed * kLookAheadSec, m_vSpringLs.m_speed * kLookAheadSec);

		const Vector speedDirLs = speedCoords.ToUnitVector();
		const Vector animVecLs = (sprungDirLs * drawRadius * 1.1f);
		const Vector speedVecLs = (speedDirLs * drawRadius * 1.1f);
		const Point animDrawPosLs = centerLs + animVecLs;
		const Point speedDrawPosLs = centerLs + speedVecLs;
		ps.EnableWireFrame();
		ps.DrawArc(centerLs, animVecLs, speedVecLs, kColorOrange, 1.0f);
		ps.DisableWireFrame();
		const Vector speedAxisLs = Cross(animVecLs, speedVecLs);
		const Vector speedTanLs = SafeNormalize(Cross(speedAxisLs, animVecLs), kUnitXAxis);

		ps.DrawArrow(centerLs + speedVecLs - (speedTanLs * 0.01f), centerLs + speedVecLs, 0.2f, kColorOrange);
		ps.DrawString(centerLs + speedVecLs, StringBuilder<128>("speed: %0.1f/%0.1f deg/s", m_hSpringLs.m_speed, m_vSpringLs.m_speed).c_str(), kColorOrangeTrans, 0.4f);
	}

	ps.DrawString(centerLs + (sprungDirLs * drawRadius * 1.1f), StringBuilder<128>("anim %0.1f/%0.1f", closestAngles.Theta(), closestAngles.Phi()).c_str(), kColorRedTrans, 0.4f);
	ps.DrawString(centerLs + (goalDirLs * drawRadius * 1.1f), StringBuilder<128>("goal %0.1f/%0.1f", compedGoal.Theta(), compedGoal.Phi()).c_str(), kColorRed, 0.4f);

	if (Dot(m_noiseQuat, kIdentity) < 0.999f)
	{
		const Vector goalPreNoiseLs = Unrotate(m_noiseQuat, goalDirLs);
		ps.DrawArrow(centerLs, (goalPreNoiseLs * drawRadius), 0.25f, kColorOrangeTrans);
		ps.DrawString(centerLs + (goalPreNoiseLs * drawRadius * 1.1f), "pre-noise goal", kColorOrangeTrans, 0.4f);
	}

	if (Dot(proxyDirLs, kUnitZAxis) < 0.999f)
	{
		const StringId64 proxyLocId = GetProxyLocatorId(pDcGesture, m_pCacheData->m_typeId);
		const Vector preSprungDirLs = m_sprungAnglesLs.ToUnitVector();
		const Vector preGoalDirLs = m_goalAnglesLs.ToUnitVector();

		ps.DrawArrow(centerLs, proxyDirLs * drawRadius * 1.1f, 0.25f, kColorCyan);
		ps.DrawString(centerLs + (proxyDirLs * drawRadius * 1.1f),
					  DevKitOnly_StringIdToString(proxyLocId),
					  kColorCyanTrans,
					  0.4f);

		ps.DrawArrow(centerLs, preSprungDirLs * drawRadius * 1.1f, 0.25f, kColorBlue);
		ps.DrawCross(centerLs + (preGoalDirLs * drawRadius), 0.1f, kColorBlueTrans);
		ps.DrawString(centerLs + (preGoalDirLs * drawRadius * 1.1f), "pre-proxy", kColorBlueTrans, 0.4f);
	}

	const DC::GestureDef* pDcDef = GetDcDef();
	if (pDcDef->m_feedbackStrength > 0.0f)
	{
		const Vector preFilterSprungDirLs = Unrotate(m_noiseQuat, Rotate(m_filteredFeedback, sprungDirLs));
		const Vector preFilterGoalDirLs = Unrotate(m_noiseQuat, Rotate(m_filteredFeedback, goalDirLs));

		if (Dot(preFilterGoalDirLs, goalDirLs) < 0.99f)
		{
			ps.DrawCross(centerLs + (preFilterGoalDirLs * drawRadius), 0.1f, kColorOrangeTrans);
		}

		if (Dot(preFilterSprungDirLs, sprungDirLs) < 0.99f)
		{
			ps.DrawArrow(centerLs, preFilterSprungDirLs * drawRadius * 1.1f, 0.25f, kColorOrangeTrans);
			ps.DrawString(centerLs + (preFilterSprungDirLs * drawRadius * 1.1f), "pre-feedback", kColorOrangeTrans, 0.5f);
		}
	}

	ps.EnableDepthTest();

	for (U32F i = 0; i < m_pCacheData->m_numBlendTris; ++i)
	{
		const I32F iA = m_pCacheData->m_blendTris[i].m_iA;
		const I32F iB = m_pCacheData->m_blendTris[i].m_iB;
		const I32F iC = m_pCacheData->m_blendTris[i].m_iC;

		const float lenA = (float(iA) * 0.01f);
		const float lenB = (float(iB) * 0.01f);
		const float lenC = (float(iC) * 0.01f);

		const SphericalCoords& coordsA = m_pCacheData->m_cachedAnims[iA].m_dir;
		const SphericalCoords& coordsB = m_pCacheData->m_cachedAnims[iB].m_dir;
		const SphericalCoords& coordsC = m_pCacheData->m_cachedAnims[iC].m_dir;

		const Vector dirA = coordsA.ToUnitVector() * (drawRadius + lenA);
		const Vector dirB = coordsB.ToUnitVector() * (drawRadius + lenB);
		const Vector dirC = coordsC.ToUnitVector() * (drawRadius + lenC);

		ps.EnableHiddenLineAlpha();

		DebugDrawSphereCoordArc(ps, centerLs, coordsA, coordsB, lenA, lenB, kColorGreen);
		DebugDrawSphereCoordArc(ps, centerLs, coordsB, coordsC, lenB, lenC, kColorGreen);
		DebugDrawSphereCoordArc(ps, centerLs, coordsC, coordsA, lenC, lenA, kColorGreen);

		ps.DisableHiddenLineAlpha();

		if (!didAlreadyDraw)
		{
			Color col = (i == selectedTri) ? kColorOrangeTrans : kColorGreenTrans;

			col.SetA(0.1f);

			DebugDrawSphereSurfaceSeg(ps, centerLs, coordsA, coordsB, coordsC, lenA, lenB, lenC, col);
		}
	}

	if (g_animOptions.m_gestures.m_drawGestureDirMeshNames)
	{
		for (U32F iAnim = 0; iAnim < m_pCacheData->m_numGestureAnims; ++iAnim)
		{
			const GestureCache::CachedAnim& anim = m_pCacheData->m_cachedAnims[iAnim];
			const Vector dirLs = anim.m_dir.ToUnitVector() * drawRadius;
			const Vec2 p = anim.m_dir.AsVec2();

			ps.DrawArrow(centerLs, dirLs * 1.25f, 0.25f, kColorGreenTrans);
			StringBuilder<256> str;

			const ArtItemAnim* pAddAnim = anim.m_hAddAnim.ToArtItem();
			const ArtItemAnim* pSlerpAnim = anim.m_hSlerpAnim.ToArtItem();

			str.append_format("[%d] ", int(iAnim));

			if (anim.m_frame >= 0.0f)
			{
				str.append_format("frame: %0.1f ", anim.m_frame);
			}

			str.append_format("(%.2f, %.2f) ", anim.m_dir.Theta(), anim.m_dir.Phi());

			if (anim.m_frame < 0.0f || (iAnim == 0))
			{
				if (wantAdd && wantSlerp)
				{
					str.append_format("\n  %s\n  %s",
									  pSlerpAnim ? pSlerpAnim->GetName() : "<null>",
									  pAddAnim ? pAddAnim->GetName() : "<null>");
				}
				else if (wantAdd)
				{
					str.append_format("%s", pAddAnim ? pAddAnim->GetName() : "<null>");
				}
				else
				{
					str.append_format("%s", pSlerpAnim ? pSlerpAnim->GetName() : "<null>");
				}
			}

			ps.DrawString(centerLs + (dirLs * 1.25f), str.c_str(), kColorWhite, 0.35f);
		}
	}

	if (g_animOptions.m_selectedGestureId == m_resolvedGestureId)
	{
		SphericalCoords min = SphericalCoords::FromThetaPhi(kLargeFloat, kLargeFloat);
		SphericalCoords max = SphericalCoords::FromThetaPhi(-kLargeFloat, -kLargeFloat);

		for (U32F iAnim = 0; iAnim < m_pCacheData->m_numGestureAnims; ++iAnim)
		{
			min = SphericalCoords::Min(min, m_pCacheData->m_cachedAnims[iAnim].m_dir);
			max = SphericalCoords::Max(max, m_pCacheData->m_cachedAnims[iAnim].m_dir);
		}

		const float drawWidth = 300.0f;
		const float drawHeight = 150.0f;

		const Vec2 drawPos = Vec2(1080.0f - (drawWidth * 0.5f), 700.0f);

		const Aabb inputScreen	= MakeAabb(Point(min.Theta(), min.Phi(), 0.0f), Point(max.Theta(), max.Phi(), 1.0f));
		const Aabb outputScreen = MakeAabb(Point(drawPos.x, drawPos.y, 0.0f),
										   Point(drawPos.x + drawWidth, drawPos.y - drawHeight, 1.0f));
		const Transform gestureRangeToScreen = MakeVirtualScreenTransform(inputScreen, outputScreen);

		PrimServerWrapper2D ps2d(gestureRangeToScreen);

		const U32F numBlendTris = m_pCacheData->m_numBlendTris;
		const GestureCache::TriangleIndicesU8* pBlendTris = m_pCacheData->m_blendTris;

		for (U32F iTri = 0; iTri < numBlendTris; ++iTri)
		{
			const Vec2 v0 = m_pCacheData->m_cachedAnims[pBlendTris[iTri].m_iA].m_dir.AsVec2();
			const Vec2 v1 = m_pCacheData->m_cachedAnims[pBlendTris[iTri].m_iB].m_dir.AsVec2();
			const Vec2 v2 = m_pCacheData->m_cachedAnims[pBlendTris[iTri].m_iC].m_dir.AsVec2();

			const Color clr = (iTri == selectedTri) ? kColorOrange : kColorGreen;
			Color transClr = clr;
			transClr.SetA(clr.A() * 0.333f);

			if (!didAlreadyDraw)
			{
				ps2d.DrawTriangle(v0, v1, v2, transClr);
			}
			ps2d.DrawLine(v0, v1, clr);
			ps2d.DrawLine(v1, v2, clr);
			ps2d.DrawLine(v2, v0, clr);
		}

		for (U32F iAnim = 0; iAnim < m_pCacheData->m_numGestureAnims; ++iAnim)
		{
			const Vector dirLs = m_pCacheData->m_cachedAnims[iAnim].m_dir.ToUnitVector() * drawRadius;
			const Vec2 p = m_pCacheData->m_cachedAnims[iAnim].m_dir.AsVec2();

			ps2d.DrawString(p, StringBuilder<32>("%d", (int)iAnim).c_str(), kColorWhite, 0.5f);
		}

		ps2d.DrawCross(Vec2(closestAngles.Theta(), closestAngles.Phi()), 10.0f, kColorRed);

		if (m_pCacheData->m_hasNoBlendDir)
		{
			const float lineX = m_pCacheData->m_noBlendDir.ToDegrees();

			const Vec2 v0(lineX, min.Phi());
			const Vec2 v1(lineX, max.Phi());

			ps2d.DrawLine(v0, v1, kColorRed);

			ps2d.DrawString(v0, "apReference-noBlend", kColorWhite, 0.5f);
		}

		ps2d.DrawLine(Vec2(min.Theta(), min.Phi()), Vec2(min.Theta(), max.Phi()), kColorBlack);
		ps2d.DrawLine(Vec2(min.Theta(), max.Phi()), Vec2(max.Theta(), max.Phi()), kColorBlack);
		ps2d.DrawLine(Vec2(max.Theta(), max.Phi()), Vec2(max.Theta(), min.Phi()), kColorBlack);
		ps2d.DrawLine(Vec2(max.Theta(), min.Phi()), Vec2(min.Theta(), min.Phi()), kColorBlack);

		g_gestureNodeDebugDrawManager.RegisterDirMesh(m_nodeUniqueId, m_resolvedGestureId, pOwner, gestureRangeToScreen);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F IGestureNode::FindNearestBlendTriangle(float hTargetDeg,
											float vTargetDeg,
											I32F islandIndex,
											SphericalCoords* pClosestAnglesOut /* = nullptr */,
											bool* pTriInsideOut /* = nullptr */) const
{
	if (!m_pCacheData || (m_pCacheData->m_numGestureAnims < 3))
	{
		return -1;
	}
	else if (m_pCacheData->m_numGestureAnims == 3 && m_pCacheData->m_numBlendTris >= 1)
	{
		return 0;
	}

	const Vec2 target = Vec2(hTargetDeg, vTargetDeg);
	float bestDist = kLargeFloat;
	I32F bestTri = -1;
	Vec2 closestPos = target;

	for (U32F iTri = 0; iTri < m_pCacheData->m_numBlendTris; ++iTri)
	{
		if (islandIndex >= 0 && m_pCacheData->m_blendTriIslandIndex[iTri] != islandIndex)
		{
			continue;
		}

		bool triangleContains = true;

		for (U32F iEdge = 0; iEdge < 3; ++iEdge)
		{
			const U32F i0 = m_pCacheData->m_blendTris[iTri].m_indices[iEdge];
			const U32F i1 = m_pCacheData-> m_blendTris[iTri].m_indices[(iEdge + 1) % 3];

			ANIM_ASSERTF(i0 < m_pCacheData->m_numGestureAnims,
						 ("Index error %d [tri: %d / %d] [gesture: '%s']",
						  (int)i0,
						  (int)iTri,
						  m_pCacheData->m_numBlendTris,
						  DevKitOnly_StringIdToString(m_resolvedGestureId)));
			ANIM_ASSERTF(i1 < m_pCacheData->m_numGestureAnims,
						 ("Index error %d [tri: %d / %d] [gesture: '%s']",
						  (int)i1,
						  (int)iTri,
						  m_pCacheData->m_numBlendTris,
						  DevKitOnly_StringIdToString(m_resolvedGestureId)));

			const Vec2 v0 = m_pCacheData->m_cachedAnims[i0].m_dir.AsVec2();
			const Vec2 v1 = m_pCacheData->m_cachedAnims[i1].m_dir.AsVec2();

			const Vec2 v0ToPt = target - v0;
			const Vec2 v0v1 = v1 - v0;
			const Vec2 perp = Vec2(-v0v1.y, v0v1.x);

			if (Dot(perp, v0ToPt) > 0.0f)
			{
				triangleContains = false;

				const Vec2 normLeg = SafeNormalize(v0v1, kZero);
				const float proj   = Limit(Dot(normLeg, v0ToPt), 0.0f, Length(v0v1));
				const Vec2 edgePt  = (normLeg * proj) + v0;
				const float dist   = Length(target - edgePt);

				if (dist < bestDist)
				{
					bestDist = dist;
					bestTri = iTri;
					closestPos = edgePt;
				}
			}
		}

		if (triangleContains)
		{
			bestDist = 0.0f;
			bestTri = iTri;
			closestPos = target;

			if (pTriInsideOut)
			{
				*pTriInsideOut = true;
			}

			break;
		}
	}

	if (pClosestAnglesOut)
	{
		*pClosestAnglesOut = SphericalCoords::FromThetaPhi(closestPos.x, closestPos.y);
	}

	return bestTri;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vec2 IGestureNode::GetBlendValues(I32F iTri, float hTargetDeg, float vTargetDeg) const
{
	if ((iTri < 0) || (iTri >= GestureCache::kMaxBlendTriangles) || !m_pCacheData)
	{
		return kZero;
	}

	// use barycentric coords to generate blend values [a,b] such that lerp(lerp(p1, p2, a), p3, b) == target
	// useful reference here: http://koblbauermath.weebly.com/uploads/1/3/1/9/13192946/barycentric_coordinates.pdf

	const SphericalCoords& coordA = m_pCacheData->m_cachedAnims[m_pCacheData->m_blendTris[iTri].m_iA].m_dir;
	const SphericalCoords& coordB = m_pCacheData->m_cachedAnims[m_pCacheData->m_blendTris[iTri].m_iB].m_dir;
	const SphericalCoords& coordC = m_pCacheData->m_cachedAnims[m_pCacheData->m_blendTris[iTri].m_iC].m_dir;

	const float x = hTargetDeg;
	const float y = vTargetDeg;

	const Vec2 p1 = Vec2(coordA.Theta(), coordA.Phi());
	const Vec2 p2 = Vec2(coordB.Theta(), coordB.Phi());
	const Vec2 p3 = Vec2(coordC.Theta(), coordC.Phi());

	const float u = (((p2.y - p3.y) * (x - p3.x)) + ((p3.x - p2.x) * (y - p3.y)))
				  / (((p2.y - p3.y) * (p1.x - p3.x)) + ((p3.x - p2.x) * (p1.y - p3.y)));

	const float v = (((p3.y - p1.y) * (x - p3.x)) + ((p1.x - p3.x) * (y - p3.y)))
				  / (((p2.y - p3.y) * (p1.x - p3.x)) + ((p3.x - p2.x) * (p1.y - p3.y)));

	const float w = 1.0f - u - v;

	const float denom = u + v;

	const float blendA = (denom == 0.0f) ? 1.0f : (v / denom);
	const float blendB = w;

	const Vec2 ret = Vec2(Limit01(blendA), Limit01(blendB));

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SphericalCoords IGestureNode::GetAngles() const
{
	float hTargetDeg = m_sprungAnglesLs.Theta();
	float vTargetDeg = m_sprungAnglesLs.Phi();

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_selectedGestureId == m_resolvedGestureId))
	{
		if (const GestureNodeDebugDrawManager::DirMesh* pDirMesh = g_gestureNodeDebugDrawManager.GetDirMesh(m_nodeUniqueId))
		{
			const Transform vScreenToRange = Inverse(pDirMesh->m_gestureRangeToScreen);

			const Mouse& mouse = EngineComponents::GetNdFrameState()->m_mouse;

			const Point mousePos(mouse.m_position.X(), mouse.m_position.Y(), 0.0f);
			const Point mouseRange = mousePos * vScreenToRange;

			if (mouse.m_buttons & kMouseButtonLeft)
			{
				const float hMouseDeg = mouseRange.X();
				const float vMouseDeg = mouseRange.Y();

				bool inside = false;
				FindNearestBlendTriangle(hMouseDeg, vMouseDeg, -1, nullptr, &inside);

				if (inside)
				{
					hTargetDeg = mouseRange.X();
					vTargetDeg = mouseRange.Y();
				}
			}
		}
	}

	return SphericalCoords::FromThetaPhi(hTargetDeg, vTargetDeg);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::SelectGestureAnims(SelectedAnimInfo* pSelectedAnimInfoOut, int islandIndex) const
{
	const SphericalCoords angles = GetAngles();

	SphericalCoords proxyComp = SphericalCoords::FromThetaPhi(0.0f, 0.0f);

	// if (m_targetValid)
	{
		const Vector proxyDirLs = GetLocalZ(m_proxyRotLs);
		proxyComp = SphericalCoords::FromVector(proxyDirLs);
	}

	const float hTargetDeg = angles.Theta() - proxyComp.Theta();
	const float vTargetDeg = angles.Phi() - proxyComp.Phi();

	const Vec2 targetAngles = Vec2(hTargetDeg, vTargetDeg);

	pSelectedAnimInfoOut->m_targetAngles = targetAngles;
	pSelectedAnimInfoOut->m_anims[0] = -1;
	pSelectedAnimInfoOut->m_anims[1] = -1;
	pSelectedAnimInfoOut->m_anims[2] = -1;

	if (!m_pCacheData || m_pCacheData->m_numGestureAnims == 0)
	{
	}
	else if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_debugGestureNodeAnimIndex >= 0)
			 && g_animOptions.IsGestureOrNoneSelected(m_resolvedGestureId))
	{
		pSelectedAnimInfoOut->m_anims[0] = g_animOptions.m_gestures.m_debugGestureNodeAnimIndex
										   % m_pCacheData->m_numGestureAnims;
	}
	else if (m_pCacheData->m_numGestureAnims == 1)
	{
		pSelectedAnimInfoOut->m_anims[0] = 0;
	}
	else if (IsSingleAnimMode())
	{
		float bestDist = kLargeFloat;

		for (U32F iAnim = 0; iAnim < m_pCacheData->m_numGestureAnims; ++iAnim)
		{
			const GestureCache::CachedAnim& anim = m_pCacheData->m_cachedAnims[iAnim];
			const Vec2 p = Vec2(anim.m_dir.Theta(), anim.m_dir.Phi());

			const float animDist = Length(targetAngles - p);
			if (animDist < bestDist)
			{
				pSelectedAnimInfoOut->m_anims[0] = iAnim;
				bestDist = animDist;
			}
		}
	}
	else if (m_pCacheData->m_numBlendTris == 0)
	{
		// all gesture points are collinear
		float bestDist	= kLargeFloat;
		I32F bestEdge	= -1;
		float bestBlend = 0.0f;

		for (U32F iEdge = 0; iEdge < m_pCacheData->m_numGestureAnims - 1; ++iEdge)
		{
			const GestureCache::CachedAnim& anim0 = m_pCacheData->m_cachedAnims[iEdge];
			const GestureCache::CachedAnim& anim1 = m_pCacheData->m_cachedAnims[iEdge + 1];

			const Vec2 p0 = Vec2(anim0.m_dir.Theta(), anim0.m_dir.Phi());
			const Vec2 p1 = Vec2(anim1.m_dir.Theta(), anim1.m_dir.Phi());

			const Vec2 leg = p1 - p0;
			const Vec2 toTarget = targetAngles - p0;

			const Vec2 normLeg	= SafeNormalize(leg, kZero);
			const float projLen = Limit(Dot(normLeg, toTarget), 0.0f, Length(leg));
			const Vec2 constrainedPoint = p0 + (normLeg * projLen);

			const float dist = Length(targetAngles - constrainedPoint);

			if (dist < bestDist)
			{
				bestDist = dist;
				bestEdge = iEdge;
				const float legLen	  = Length(leg);
				const float invLegLen = (legLen > kSmallFloat) ? (1.0f / legLen) : 0.0f;
				bestBlend = projLen * invLegLen;
			}
		}

		if (bestEdge >= 0)
		{
			pSelectedAnimInfoOut->m_anims[0] = bestEdge;
			pSelectedAnimInfoOut->m_anims[1] = bestEdge + 1;
			pSelectedAnimInfoOut->m_blend	 = Vec2(bestBlend, 0.0f);
		}
	}
	else
	{
		SphericalCoords closestAngles;
		const I32F selectedTri = FindNearestBlendTriangle(hTargetDeg, vTargetDeg, islandIndex, &closestAngles);
		if (selectedTri >= 0)
		{
			pSelectedAnimInfoOut->m_anims[0] = m_pCacheData->m_blendTris[selectedTri].m_iA;
			pSelectedAnimInfoOut->m_anims[1] = m_pCacheData->m_blendTris[selectedTri].m_iB;
			pSelectedAnimInfoOut->m_anims[2] = m_pCacheData->m_blendTris[selectedTri].m_iC;

			pSelectedAnimInfoOut->m_blend = GetBlendValues(selectedTri, closestAngles.Theta(), closestAngles.Phi());
		}
	}

	ANIM_ASSERT(pSelectedAnimInfoOut->m_anims[0] < m_pCacheData->m_numGestureAnims);
	ANIM_ASSERT(pSelectedAnimInfoOut->m_anims[1] < m_pCacheData->m_numGestureAnims);
	ANIM_ASSERT(pSelectedAnimInfoOut->m_anims[2] < m_pCacheData->m_numGestureAnims);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float IGestureNode::GetSampleFrame(const GestureAnim& anim,
								   bool additive,
								   const float statePhase,
								   float* pOutTotalFrames) const
{
	const float phase = GetSamplePhase(anim, statePhase);

	float totalFrames = -1.0f;
	const ArtItemAnim* pArtItemAnim = additive ? anim.m_hAddAnim.ToArtItem() : anim.m_hSlerpAnim.ToArtItem();
	const ndanim::ClipData* pClipData = pArtItemAnim ? pArtItemAnim->m_pClipData : nullptr;

	if (pClipData)
	{
		totalFrames = pClipData->m_numTotalFrames;
	}

	if (totalFrames < 0.0f)
		return -1.0f;

	const float sample = phase * (totalFrames - 1.0f);

	if (pOutTotalFrames)
		*pOutTotalFrames = totalFrames;

	return sample;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float IGestureNode::GetSamplePhase(const GestureAnim& anim, const float statePhase) const
{
	if (!m_pCacheData)
		return 0.0f;

	const float nodePhase = m_pCacheData->m_useDetachedPhase ? m_detachedPhase : statePhase;

	const float phase = Limit01((anim.m_phase >= 0.0f) ? anim.m_phase : nodePhase);

	return phase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IGestureNode::AddCmdForAnimation(AnimCmdList* pAnimCmdList,
									  I32F index,
									  I32F outputInstance,
									  const AnimCmdGenInfo* pCmdGenInfo,
									  bool additive) const
{
	if (index < 0)
		return false;

	bool validAnimData = false;

	if (const ArtItemAnim* pAnim = GetAnimFromIndex(index, additive))
	{
		const float sample = GetSampleFrame(m_pCacheData->m_cachedAnims[index],
											additive,
											pCmdGenInfo->m_statePhase,
											nullptr);

		pAnimCmdList->AddCmd_EvaluateClip(pAnim, outputInstance, sample);
		validAnimData = true;
	}
	else
	{
		pAnimCmdList->AddCmd_EvaluateEmptyPose(outputInstance);
	}

	return validAnimData;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IGestureNode::AddCmdForBlend(AnimCmdList* pAnimCmdList,
								  I32F indexA,
								  I32F indexB,
								  float blend,
								  I32F outputInstance,
								  const AnimCmdGenInfo* pCmdGenInfo,
								  bool additive) const
{
	const ArtItemAnim* pAnimA = GetAnimFromIndex(indexA, additive);
	const ArtItemAnim* pAnimB = GetAnimFromIndex(indexB, additive);

	bool valid = false;

	if (pAnimA && pAnimB)
	{
		AddCmdForAnimation(pAnimCmdList, indexA, outputInstance, pCmdGenInfo, additive);
		AddCmdForAnimation(pAnimCmdList, indexB, outputInstance + 1, pCmdGenInfo, additive);

		pAnimCmdList->AddCmd_EvaluateBlend(outputInstance,
										   outputInstance + 1,
										   outputInstance,
										   ndanim::kBlendSlerp,
										   blend);

		valid = true;
	}
	else if (pAnimA)
	{
		AddCmdForAnimation(pAnimCmdList, indexA, outputInstance, pCmdGenInfo, additive);

		valid = true;
	}
	else if (pAnimB)
	{
		AddCmdForAnimation(pAnimCmdList, indexB, outputInstance, pCmdGenInfo, additive);

		valid = true;
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::ForAllAnimationsInternal_Selected(const SelectedAnimInfo& anims,
													 bool additive,
													 const AnimStateSnapshot* pSnapshot,
													 AnimationVisitFunc visitFunc,
													 float combinedBlend,
													 uintptr_t userData) const
{
	const float statePhase = pSnapshot->m_statePhase;

	const I32F anim0 = anims.m_anims[0];
	const I32F anim1 = anims.m_anims[1];
	const I32F anim2 = anims.m_anims[2];

	const bool stage0valid = anim0 >= 0;
	const bool stage1valid = anim1 >= 0;
	const bool stage2valid = anim2 >= 0;

	if (stage0valid && stage1valid && stage2valid)
	{
		const float blend0 = combinedBlend * (1.0f - anims.m_blend.x) * (1.0f - anims.m_blend.y);
		const float blend1 = combinedBlend * anims.m_blend.x * (1.0f - anims.m_blend.y);
		const float blend2 = combinedBlend * anims.m_blend.y;

		const float phase0 = GetSamplePhase(m_pCacheData->m_cachedAnims[anim0], statePhase);
		const float phase1 = GetSamplePhase(m_pCacheData->m_cachedAnims[anim0], statePhase);
		const float phase2 = GetSamplePhase(m_pCacheData->m_cachedAnims[anim0], statePhase);

		visitFunc(GetAnimFromIndex(anim0, additive), pSnapshot, this, blend0, phase0, userData);
		visitFunc(GetAnimFromIndex(anim1, additive), pSnapshot, this, blend1, phase1, userData);
		visitFunc(GetAnimFromIndex(anim2, additive), pSnapshot, this, blend2, phase2, userData);
	}
	else if (stage0valid && stage1valid)
	{
		const float blend0 = combinedBlend * (1.0f - anims.m_blend.x);
		const float blend1 = combinedBlend * anims.m_blend.x;

		const float phase0 = GetSamplePhase(m_pCacheData->m_cachedAnims[anim0], statePhase);
		const float phase1 = GetSamplePhase(m_pCacheData->m_cachedAnims[anim0], statePhase);

		visitFunc(GetAnimFromIndex(anim0, additive), pSnapshot, this, blend0, phase0, userData);
		visitFunc(GetAnimFromIndex(anim1, additive), pSnapshot, this, blend1, phase1, userData);
	}
	else if (stage0valid)
	{
		const float phase0 = GetSamplePhase(m_pCacheData->m_cachedAnims[anim0], statePhase);

		visitFunc(GetAnimFromIndex(anim0, additive), pSnapshot, this, combinedBlend, phase0, userData);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::ForAllAnimationsInternal(const AnimStateSnapshot* pSnapshot,
											AnimationVisitFunc visitFunc,
											float combinedBlend,
											uintptr_t userData) const
{
	if (!visitFunc)
		return;

	bool wantAdd   = false;
	bool wantSlerp = false;

	WantAddOrSlerp(wantAdd, wantSlerp);

	if (m_islandTransition.m_transitioning)
	{
		SelectedAnimInfo animInfoFrom, animInfoTo;

		const float transitionTT = m_islandTransition.TransitionParameter();

		SelectGestureAnims(&animInfoFrom, m_currentIslandIndex);
		SelectGestureAnims(&animInfoTo, m_islandTransition.m_targetIsland);

		if (wantSlerp)
		{
			ForAllAnimationsInternal_Selected(animInfoFrom,
											  false,
											  pSnapshot,
											  visitFunc,
											  (1.0f - transitionTT) * combinedBlend,
											  userData);
			ForAllAnimationsInternal_Selected(animInfoTo,
											  false,
											  pSnapshot,
											  visitFunc,
											  transitionTT * combinedBlend,
											  userData);
		}

		if (wantAdd)
		{
			ForAllAnimationsInternal_Selected(animInfoFrom,
											  true,
											  pSnapshot,
											  visitFunc,
											  (1.0f - transitionTT) * combinedBlend,
											  userData);
			ForAllAnimationsInternal_Selected(animInfoTo,
											  true,
											  pSnapshot,
											  visitFunc,
											  transitionTT * combinedBlend,
											  userData);
		}
	}
	else
	{
		SelectedAnimInfo animInfo;

		SelectGestureAnims(&animInfo, m_currentIslandIndex);

		if (wantSlerp)
		{
			ForAllAnimationsInternal_Selected(animInfo, false, pSnapshot, visitFunc, combinedBlend, userData);
		}

		if (wantAdd)
		{
			ForAllAnimationsInternal_Selected(animInfo, true, pSnapshot, visitFunc, combinedBlend, userData);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IGestureNode::IsContiguousWith(const GestureCache::CacheKey& newCacheKey,
									const BoundFrame& newOwnerLoc,
									bool requireExact) const
{
	if (m_resolvedGestureId != newCacheKey.m_gestureId)
	{
		return false;
	}

	if (requireExact)
	{
		if (m_cacheKey.m_u64 != newCacheKey.m_u64)
		{
			return false;
		}

		const Locator locWs = m_ownerLoc.GetLocatorWs();
		const Locator newLocWs = newOwnerLoc.GetLocatorWs();

		const bool closeDist = Dist(locWs.Pos(), newLocWs.Pos()) < 0.01f;
		const bool closeRot = Dot(locWs.Rot(), newLocWs.Rot()) > 0.99f;

		if (!closeDist || !closeRot)
		{
			return false;
		}

	}

	if (IsSingleAnimMode())
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
SnapshotNodeHeap::Index IGestureNode::SnapshotNodeFunc(AnimStateSnapshot* pSnapshot,
													   const DC::AnimNode* pDcAnimNode,
													   const SnapshotAnimNodeTreeParams& params,
													   SnapshotAnimNodeTreeResults& results)
{
	SnapshotNodeHeap* pHeap = pSnapshot->m_pSnapshotHeap;

	SnapshotNodeHeap::Index returnNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
	SnapshotNodeHeap::Index gestureNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;

	ResolvedGestureInfo info;
	const DC::AnimNodeGesture* pDcGestureNode = (const DC::AnimNodeGesture*)pDcAnimNode;

	StringBuilder<AnimSnapshotNodeEmptyPose::kErrorStringBufLen> snapshotErrorString;

	if (!ResolveGesture(pDcGestureNode, params, &info, &snapshotErrorString))
	{
		if (AnimSnapshotNodeEmptyPose* pEmptyPoseNode = pHeap->AllocateNode<AnimSnapshotNodeEmptyPose>(&returnNodeIndex))
		{
			pEmptyPoseNode->SnapshotNode(pSnapshot, pDcAnimNode, params, results);

			pEmptyPoseNode->SetErrorString(snapshotErrorString.c_str());
		}
		else
		{
			returnNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
		}

		return returnNodeIndex;
	}

	IGestureNode* pNewGestureNode = nullptr;

	if (info.m_gestureType == Gesture::AnimType::kCombo)
	{
		pNewGestureNode = pHeap->AllocateNode<AnimNodeGestureCombo>(&gestureNodeIndex);
	}
	else
	{
		pNewGestureNode = pHeap->AllocateNode<AnimNodeGestureLeaf>(&gestureNodeIndex);
	}

	if (pNewGestureNode && pNewGestureNode->TrySnapshotNode(pSnapshot, info, params, results, &snapshotErrorString))
	{
		const DC::GestureDef* pDcGesture = pNewGestureNode->GetDcDef();

		returnNodeIndex = gestureNodeIndex;

		if (pDcGesture && pDcGesture->m_ik
			&& (pDcGesture->m_ik->m_config || (pDcGesture->m_ik->m_configName != INVALID_STRING_ID_64)))
		{
			DC::AnimNodeIk dcIkNode;
			dcIkNode.m_config	  = pDcGesture->m_ik->m_config;
			dcIkNode.m_configName = pDcGesture->m_ik->m_configName;
			dcIkNode.m_child	  = nullptr;
			dcIkNode.m_dcType	  = SID("anim-node-ik");
			dcIkNode.m_configFunc = nullptr;

			const SnapshotNodeHeap::Index ikNodeIndex = AnimSnapshotNodeIK::SnapshotAnimNode(pSnapshot,
																							 &dcIkNode,
																							 params,
																							 results);

			if (AnimSnapshotNodeIK* pIkNode = pHeap->GetNodeByIndex<AnimSnapshotNodeIK>(ikNodeIndex))
			{
				pIkNode->SetChildIndex(returnNodeIndex);
				returnNodeIndex = ikNodeIndex;
			}
		}

		if (pNewGestureNode->WantJointLimits() && !pDcGesture->m_jointLimitsId)
		{
			SnapshotNodeHeap::Index parentNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;

			if (AnimNodeJointLimits* pJointLimitsNode = pHeap->AllocateNode<AnimNodeJointLimits>(&parentNodeIndex))
			{
				NdGameObjectHandle hGo = params.m_pAnimData->m_hProcess.CastHandleType<MutableNdGameObjectHandle>();
				pJointLimitsNode->SetCustomLimits(hGo, pDcGesture->m_jointLimitsId);
				pJointLimitsNode->SetChildIndex(returnNodeIndex);
				returnNodeIndex = parentNodeIndex;
			}
		}
	}
	else
	{
		if (pNewGestureNode)
		{
			pHeap->ReleaseNode(gestureNodeIndex);
			pNewGestureNode = nullptr;
		}

		if (AnimSnapshotNodeEmptyPose* pEmptyPoseNode = pHeap->AllocateNode<AnimSnapshotNodeEmptyPose>(&returnNodeIndex))
		{
			pEmptyPoseNode->SnapshotNode(pSnapshot, pDcAnimNode, params, results);

			pEmptyPoseNode->SetErrorString(snapshotErrorString.c_str());
		}
		else
		{
			returnNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
		}
	}

	return returnNodeIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IGestureNode::OnAddedToBlendNode(const SnapshotAnimNodeTreeParams& params,
									  AnimStateSnapshot* pSnapshot,
									  AnimSnapshotNodeBlend* pBlendNode,
									  bool leftNode)
{
	if (!m_pCacheData || leftNode)
		return;

	if (m_baseLayerGesture)
	{
		if (m_useLegFixIk)
		{
			pBlendNode->m_flags |= DC::kAnimNodeBlendFlagLegFixIk;
		}
		else
		{
			pBlendNode->m_flags &= ~DC::kAnimNodeBlendFlagLegFixIk;
		}

		if (m_handFixIkMask & DC::kHandIkHandLeft)
		{
			pBlendNode->m_flags |= DC::kAnimNodeBlendFlagHandFixIkLeft;
		}
		else
		{
			pBlendNode->m_flags &= ~DC::kAnimNodeBlendFlagHandFixIkLeft;
		}

		if (m_handFixIkMask & DC::kHandIkHandRight)
		{
			pBlendNode->m_flags |= DC::kAnimNodeBlendFlagHandFixIkRight;
		}
		else
		{
			pBlendNode->m_flags &= ~DC::kAnimNodeBlendFlagHandFixIkRight;
		}
	}

	if ((m_pCacheData->m_featherBlendId != INVALID_STRING_ID_64) && (pBlendNode->m_featherBlendIndex < 0))
	{
		pBlendNode->m_featherBlendIndex = g_featherBlendTable.LoginFeatherBlend(m_pCacheData->m_featherBlendId,
																				params.m_pAnimData);
	}

	m_baseAnimIndex = pBlendNode->m_leftIndex;

	if (m_cacheKey.m_gestureNodeType == Gesture::AnimType::kCombo)
	{
		pBlendNode->m_flags |= DC::kAnimNodeBlendFlagCustomBlendOp;
	}
/*
	else
	{
		pBlendNode->m_flags |= DC::kAnimNodeBlendFlagEvaluateBothNodes;
	}
*/

	const DC::GestureDef* pDcDef = m_dcDef;
	if (WantJointLimits() && pDcDef->m_jointLimitsId)
	{
		pBlendNode->m_flags |= DC::kAnimNodeBlendFlagApplyJointLimits;
		pBlendNode->m_customLimitsId = pDcDef->m_jointLimitsId;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::GestureAnims* IGestureNode::GetDcAnimsDef() const
{
	return Gesture::GetGestureAnims(m_dcDef, m_cacheKey.m_altIndex);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IGestureNode::DeterminePropAnim(SsAnimateParams* pAnimParams, float statePhase) const
{
	if (!m_hProp.Assigned() || !pAnimParams)
		return false;

	const DC::GestureAnims* pAnimsDef = GetDcAnimsDef();
	if (!pAnimsDef)
		return false;

	if (!m_pCacheData || (pAnimsDef->m_numAnimPairs != m_pCacheData->m_numGestureAnims))
		return false;

	SelectedAnimInfo animInfo;

	SelectGestureAnims(&animInfo, m_currentIslandIndex);

	I32F iBest = -1;
	float bestDist = kLargeFloat;

	for (U32F i = 0; i < 3; ++i)
	{
		const I32F iAnim = animInfo.m_anims[i];

		if (iAnim < 0 || (iAnim > m_pCacheData->m_numGestureAnims))
			continue;

		const float dist = Length(m_pCacheData->m_cachedAnims[iAnim].m_dir.AsVec2() - animInfo.m_targetAngles);

		if (dist < bestDist)
		{
			iBest = iAnim;
			bestDist = dist;
		}
	}

	if (iBest < 0)
	{
		return false;
	}

	const StringId64 animId = pAnimsDef->m_animPairs[iBest].m_propAnim;

	if (animId == INVALID_STRING_ID_64)
	{
		return false;
	}

	pAnimParams->m_nameId = animId;

	if (m_pCacheData->m_useDetachedPhase)
	{
		pAnimParams->m_startPhase = m_detachedPhase;
	}
	else if (m_pCacheData->m_cachedAnims[iBest].m_phase >= 0.0f)
	{
		pAnimParams->m_startPhase = m_pCacheData->m_cachedAnims[iBest].m_phase;
		pAnimParams->m_speed = 0.0f;
	}
	else
	{
		pAnimParams->m_startPhase = statePhase;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Quat IGestureNode::GetNoiseQuat() const
{
	const DC::GestureDef* pDcGesture = m_dcDef;
	if (!pDcGesture)
	{
		return kIdentity;
	}

	const DC::GestureNoiseDef* pNoiseDef = pDcGesture ? pDcGesture->m_offsetNoise : nullptr;

	if (pDcGesture->m_offsetNoiseAlternatives && (m_requestedAltNoise >= 0)
		&& (m_requestedAltNoise < pDcGesture->m_offsetNoiseAlternatives->m_numEntries))
	{
		const void* pAlt = pDcGesture->m_offsetNoiseAlternatives->m_values[m_requestedAltNoise].m_ptr;
		pNoiseDef = (const DC::GestureNoiseDef*)pAlt;
	}

	if (!pNoiseDef || (pNoiseDef->m_dataSize < 1))
	{
		return kIdentity;
	}

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_disableNoiseOffsets))
	{
		return kIdentity;
	}

	const float fNumFrames = float(pNoiseDef->m_dataSize - 1);
	const float noiseDuration = fNumFrames / 30.0f; // fixed 30Hz data for now

	const Clock* pClock = EngineComponents::GetNdFrameState()->GetClock(kGameClock);

	const float curTimeSec = ToSeconds(pClock->GetCurTime());

	const float noisePhase = static_cast<float>(Fmod(curTimeSec / noiseDuration));
	const float fSample = noisePhase * fNumFrames;
	const float fSample0 = Floor(fSample);
	const float fSample1 = Ceil(fSample);
	const float frameTT = fSample - fSample0;

	const U32F sample0 = U32F(fSample0);
	const U32F sample1 = U32F(fSample1);

	const Vector delta0Ls = pNoiseDef->m_deltasLs[sample0];
	const Vector delta1Ls = pNoiseDef->m_deltasLs[sample1];
	const Vector dirLs = SafeNormalize(Slerp(delta0Ls, delta1Ls, frameTT), kUnitZAxis);

	const Quat deltaQuat = QuatFromVectors(kUnitZAxis, dirLs);

	if (false)
	{
		const Locator alignWs = m_ownerLoc.GetLocatorWs();
		const Locator gestureSpaceWs = alignWs.TransformLocator(m_gestureSpaceOs);
		const Point centerWs = alignWs.TransformPoint(m_gestureOriginOs);
		const Vector axisWs = gestureSpaceWs.TransformVector(GetLocalZ(deltaQuat));

		g_prim.Draw(DebugArrow(centerWs, axisWs));
		//g_prim.Draw(DebugString(centerWs + axisWs, StringBuilder<64>("%0.1fdeg", RADIANS_TO_DEGREES(deltaAngle * 5.0f)).c_str(), kColorWhite, 0.5f));
	}

	return deltaQuat;
}

/*********************************************************************************************************************/
/*********************************************************************************************************************/
/*********************************************************************************************************************/

/// --------------------------------------------------------------------------------------------------------------- ///
AnimNodeGestureLeaf::AnimNodeGestureLeaf(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex)
	: ParentClass(typeId, dcTypeId, nodeIndex)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimNodeGestureLeaf::GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
												AnimCmdList* pAnimCmdList,
												I32F outputInstance,
												const AnimCmdGenInfo* pCmdGenInfo) const
{
	PROFILE_ACCUM(GestureNode_GenAnimCmds);

	if ((m_resolvedGestureId == INVALID_STRING_ID_64)
		|| !m_pCacheData
		|| m_pCacheData->m_isBad
		|| (m_pCacheData->m_numGestureAnims == 0)
		|| FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_disableGestureNodes))
	{
		pAnimCmdList->AddCmd_EvaluateEmptyPose(outputInstance);
		return;
	}

	if (m_loddedOut)
	{
		if (const ArtItemAnim* pLodAnim = m_pCacheData->m_hLowLodAnim.ToArtItem())
		{
			pAnimCmdList->AddCmd_EvaluateClip(pLodAnim, outputInstance, m_dcDef->m_lowLodPoseFrame);

			if (GestureAnimsMirrored())
			{
				pAnimCmdList->AddCmd_EvaluateFlip(outputInstance);
			}
		}
		else
		{
			pAnimCmdList->AddCmd_EvaluateEmptyPose(outputInstance);
		}

		return;
	}

	const bool wantAdd = IsAdditive();

	GenerateAnimCommands_Internal(pSnapshot, pAnimCmdList, outputInstance, pCmdGenInfo, wantAdd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimNodeGestureLeaf::GenerateAnimCommands_PreBlend(const AnimStateSnapshot* pSnapshot,
														AnimCmdList* pAnimCmdList,
														const AnimCmdGenInfo* pCmdGenInfo,
														const AnimSnapshotNodeBlend* pBlendNode,
														bool leftNode,
														I32F leftInstance,
														I32F rightInstance,
														I32F outputInstance) const
{
	if (!m_pCacheData)
		return;

	if (leftNode)
	{
		return;
	}

	const DC::GestureDef* pDcDef = GetDcDef();
	if (!pDcDef || pDcDef->m_spacesUsePrevFrame)
	{
		return;
	}

	if ((m_pCacheData->m_animSpace.m_jointId != INVALID_STRING_ID_64)
		|| (m_pCacheData->m_originSpace.m_jointId != INVALID_STRING_ID_64))
	{
		GestureAnimPluginData cmd;
		cmd.m_pSourceNode = const_cast<AnimNodeGestureLeaf*>(this);
		cmd.m_sourceInstance = leftInstance;

		pAnimCmdList->AddCmd_EvaluateAnimPhasePlugin(SID("gesture-preanim"), &cmd);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimNodeGestureLeaf::GenerateAnimCommands_PostBlend(const AnimStateSnapshot* pSnapshot,
														 AnimCmdList* pAnimCmdList,
														 const AnimCmdGenInfo* pCmdGenInfo,
														 const AnimSnapshotNodeBlend* pBlendNode,
														 bool leftNode,
														 I32F leftInstance,
														 I32F rightInstance,
														 I32F outputInstance) const
{
	if (!m_pCacheData)
		return;

	if (leftNode)
	{
		return;
	}

	const DC::GestureDef* pDcDef = GetDcDef();

	if (!pDcDef)
	{
		return;
	}

	const bool spacesUsePrevFrame = pDcDef->m_spacesUsePrevFrame;
	const bool disableFeedback	  = pDcDef->m_disableFeedback;

 	const bool updateSpaces = ((m_pCacheData->m_animSpace.m_jointId != INVALID_STRING_ID_64)
 							   || (m_pCacheData->m_originSpace.m_jointId != INVALID_STRING_ID_64))
 							  && !spacesUsePrevFrame;

	const bool enableFeedback = !spacesUsePrevFrame && !disableFeedback
								&& (m_pCacheData->m_feedbackSpace.m_jointId != INVALID_STRING_ID_64)
								&& !IsSingleAnimMode();

	const bool enableFixup = (pDcDef->m_fixupIk != nullptr);

	const bool doPostAnim = enableFeedback || enableFixup;

	if (!doPostAnim)
	{
		return;
	}

	GestureAnimPluginData cmd;
	cmd.m_pSourceNode = const_cast<AnimNodeGestureLeaf*>(this);
	cmd.m_sourceInstance = outputInstance;

	pAnimCmdList->AddCmd_EvaluateAnimPhasePlugin(SID("gesture-postblend"), &cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ANIM_NODE_REGISTER_WITH_SNAPSHOT_FUNC(AnimNodeGestureLeaf,
									  IGestureNode,
									  SID("anim-node-gesture"),
									  IGestureNode::SnapshotNodeFunc);

FROM_ANIM_NODE_DEFINE(AnimNodeGestureLeaf);

/*********************************************************************************************************************/
/*********************************************************************************************************************/
/*********************************************************************************************************************/

/// --------------------------------------------------------------------------------------------------------------- ///
ANIM_NODE_REGISTER_WITH_SNAPSHOT_FUNC(AnimNodeGestureCombo,
									  IGestureNode,
									  SID("anim-node-gesture-combo"), // doesn't actually exist, but we don't need one
									  IGestureNode::SnapshotNodeFunc);

FROM_ANIM_NODE_DEFINE(AnimNodeGestureCombo);

/*********************************************************************************************************************/
/*********************************************************************************************************************/
/*********************************************************************************************************************/

/// --------------------------------------------------------------------------------------------------------------- ///
AnimNodeGestureCombo::AnimNodeGestureCombo(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex)
	: ParentClass(typeId, dcTypeId, nodeIndex)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimNodeGestureCombo::GenerateAnimCommands_CustomBlend(const AnimSnapshotNodeBlend* pParentBlendNode,
															const AnimSnapshotNode* pChildNode,
															float nodeBlendToUse,
															const AnimStateSnapshot* pSnapshot,
															AnimCmdList* pAnimCmdList,
															I32F outputInstance,
															const AnimCmdGenInfo* pCmdGenInfo) const
{
	if (pChildNode)
	{
		pChildNode->GenerateAnimCommands(pSnapshot, pAnimCmdList, outputInstance, pCmdGenInfo);
	}
	else
	{
		pAnimCmdList->AddCmd_EvaluateEmptyPose(outputInstance);
	}

	if ((m_resolvedGestureId == INVALID_STRING_ID_64)
		|| !m_pCacheData
		|| m_pCacheData->m_isBad
		|| (m_pCacheData->m_numGestureAnims == 0)
		|| FALSE_IN_FINAL_BUILD(g_animOptions.m_gestures.m_disableGestureNodes))
	{
		return;
	}

	if (m_loddedOut)
	{
		if (const ArtItemAnim* pLodAnim = m_pCacheData->m_hLowLodAnim.ToArtItem())
		{
			const bool additive = pLodAnim->IsAdditive();
			const bool mirrored = GestureAnimsMirrored();

			if (additive && mirrored)
			{
				pAnimCmdList->AddCmd_EvaluateFlip(outputInstance);
			}

			pAnimCmdList->AddCmd_EvaluateClip(pLodAnim, outputInstance + 1, m_dcDef->m_lowLodPoseFrame);

			if (!additive && mirrored)
			{
				pAnimCmdList->AddCmd_EvaluateFlip(outputInstance + 1);
			}

			const ndanim::BlendMode blend = additive ? ndanim::kBlendAdditive : ndanim::kBlendSlerp;
			pAnimCmdList->AddCmd_EvaluateBlend(outputInstance, outputInstance + 1, outputInstance, blend, 1.0f);

			if (additive && mirrored)
			{
				pAnimCmdList->AddCmd_EvaluateFlip(outputInstance);
			}
		}

		return;
	}

	const DC::GestureDef* pDcDef = GetDcDef();

	const bool spacesUsePrevFrame = pDcDef && pDcDef->m_spacesUsePrevFrame;
	const bool disableFeedback	  = pDcDef && pDcDef->m_disableFeedback;

 	const bool updateSpaces = ((m_pCacheData->m_animSpace.m_jointId != INVALID_STRING_ID_64)
 							   || (m_pCacheData->m_originSpace.m_jointId != INVALID_STRING_ID_64))
 							  && !spacesUsePrevFrame;

	const bool enableFeedback = !spacesUsePrevFrame && !disableFeedback
								&& (m_pCacheData->m_feedbackSpace.m_jointId != INVALID_STRING_ID_64)
								&& !IsSingleAnimMode();

	const bool enableFixup = (pDcDef->m_fixupIk != nullptr);

	if (updateSpaces)
	{
		GestureAnimPluginData cmd;
		cmd.m_pSourceNode = const_cast<AnimNodeGestureCombo*>(this);
		cmd.m_sourceInstance = outputInstance;

		pAnimCmdList->AddCmd_EvaluateAnimPhasePlugin(SID("gesture-preanim"), &cmd);
	}

	const I32F nodeBreadth = CalculateBreadth();

	if (pParentBlendNode->m_flags & DC::kAnimNodeBlendFlagLegFixIk)
	{
		AnimSnapshotNodeLegFixIk::GenerateLegFixIkCommands_PreEval(pAnimCmdList, outputInstance, nodeBreadth);
	}

	const bool wantHandFixIk = pParentBlendNode->m_flags & (DC::kAnimNodeBlendFlagHandFixIkLeft | DC::kAnimNodeBlendFlagHandFixIkRight);

	if (wantHandFixIk)
	{
		AnimSnapshotNodeHandFixIk::GenerateHandFixIkCommands_PreEval(pAnimCmdList, outputInstance, nodeBreadth);
	}

	const OrbisAnim::ChannelFactor* const* ppChannelFactors = nullptr;
	const U32* pNumChannelFactors = nullptr;

	if (pParentBlendNode->m_featherBlendIndex >= 0)
	{
		nodeBlendToUse = g_featherBlendTable.CreateChannelFactorsForAnimCmd(pCmdGenInfo->m_pContext->m_pAnimateSkel,
																			pParentBlendNode->m_featherBlendIndex,
																			nodeBlendToUse,
																			&ppChannelFactors,
																			&pNumChannelFactors);
	}

	{
		GenerateAnimCommands_Internal(pSnapshot, pAnimCmdList, outputInstance + 1, pCmdGenInfo, false);

		if (ppChannelFactors)
		{
			pAnimCmdList->AddCmd_EvaluateFeatherBlend(outputInstance,
													  outputInstance + 1,
													  outputInstance,
													  ndanim::kBlendSlerp,
													  nodeBlendToUse,
													  ppChannelFactors,
													  pNumChannelFactors,
													  pParentBlendNode->m_featherBlendIndex);
		}
		else
		{
			pAnimCmdList->AddCmd_EvaluateBlend(outputInstance,
											   outputInstance + 1,
											   outputInstance,
											   ndanim::kBlendSlerp,
											   nodeBlendToUse);
		}
	}

	{
		GenerateAnimCommands_Internal(pSnapshot, pAnimCmdList, outputInstance + 1, pCmdGenInfo, true);

		if (ppChannelFactors)
		{
			pAnimCmdList->AddCmd_EvaluateFeatherBlend(outputInstance,
													  outputInstance + 1,
													  outputInstance,
													  ndanim::kBlendAdditive,
													  nodeBlendToUse,
													  ppChannelFactors,
													  pNumChannelFactors,
													  pParentBlendNode->m_featherBlendIndex);
		}
		else
		{
			pAnimCmdList->AddCmd_EvaluateBlend(outputInstance,
											   outputInstance + 1,
											   outputInstance,
											   ndanim::kBlendAdditive,
											   nodeBlendToUse);
		}
	}

	if (pParentBlendNode->m_flags & DC::kAnimNodeBlendFlagLegFixIk)
	{
		AnimSnapshotNodeLegFixIk::GenerateLegFixIkCommands_PostEval(pAnimCmdList, outputInstance, nodeBreadth);
	}

	if (wantHandFixIk)
	{
		HandFixIkPluginCallbackArg arg;
		arg.m_handsToIk[kLeftArm]  = pParentBlendNode->m_flags & DC::kAnimNodeBlendFlagHandFixIkLeft;
		arg.m_handsToIk[kRightArm] = pParentBlendNode->m_flags & DC::kAnimNodeBlendFlagHandFixIkRight;

		AnimSnapshotNodeHandFixIk::GenerateHandFixIkCommands_PostEval(pAnimCmdList, outputInstance, nodeBreadth, &arg);
	}

	const bool doPostAnim = enableFeedback || enableFixup;

	if (doPostAnim)
	{
		GestureAnimPluginData cmd;
		cmd.m_pSourceNode = const_cast<AnimNodeGestureCombo*>(this);
		cmd.m_sourceInstance = outputInstance;

		pAnimCmdList->AddCmd_EvaluateAnimPhasePlugin(SID("gesture-postblend"), &cmd);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct GestureNodeSearchFuncData
{
	GestureCache::CacheKey m_cacheKey;
	BoundFrame m_ownerLoc;
	bool m_requireExact = true;
	const IGestureNode* m_pFoundNode = nullptr;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool GestureNodeSearchFunc(const AnimStateSnapshot* pSnapshot,
								  const AnimSnapshotNode* pNode,
								  const AnimSnapshotNodeBlend* pParentBlendNode,
								  float combinedBlend,
								  uintptr_t userData)
{
	if (!pNode || !pNode->IsKindOf(g_type_IGestureNode))
		return true;

	GestureNodeSearchFuncData& data = *(GestureNodeSearchFuncData*)userData;

	if (data.m_pFoundNode)
		return false;

	const IGestureNode* pGestureNode = (const IGestureNode*)pNode;

	if (pGestureNode->IsContiguousWith(data.m_cacheKey, data.m_ownerLoc, data.m_requireExact))
	{
		data.m_pFoundNode = pGestureNode;
	}

	return data.m_pFoundNode == nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const IGestureNode* FindGestureNodeToInheritFrom(const GestureCache::CacheKey& newCacheKey,
														const BoundFrame& ownerLoc,
														bool requireExact,
														const SnapshotAnimNodeTreeParams& params)
{
	const AnimStateInstance* pPrevInst = params.m_pFadeToStateParams ? params.m_pFadeToStateParams->m_pPrevInstance
																	 : nullptr;
	if (!pPrevInst)
		return nullptr;

	GestureNodeSearchFuncData data;
	data.m_cacheKey		= newCacheKey;
	data.m_ownerLoc		= ownerLoc;
	data.m_requireExact = requireExact;

	if (const AnimSnapshotNode* pRootNode = pPrevInst->GetRootSnapshotNode())
	{
		pRootNode->VisitNodesOfKind(&pPrevInst->GetAnimStateSnapshot(),
									SID("IGestureNode"),
									GestureNodeSearchFunc,
									(uintptr_t)&data);
	}

	return data.m_pFoundNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct VisitTargetPointParams
{
	StringId64 m_typeId = INVALID_STRING_ID_64;
	Point m_targetPosWs = kOrigin;
	bool m_smoothed = false;
	bool m_valid = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool GestureNodeVisitFuncGetTargetPoint(const AnimStateSnapshot* pSnapshot,
											   const AnimSnapshotNode* pNode,
											   const AnimSnapshotNodeBlend* pParentBlendNode,
											   float combinedBlend,
											   uintptr_t userData)
{
	const IGestureNode* pGestureNode = IGestureNode::FromAnimNode(pNode);
	if (!pGestureNode)
		return true;

	VisitTargetPointParams* pParams = (VisitTargetPointParams*)userData;

	const bool matches = (pParams->m_typeId == INVALID_STRING_ID_64)
						 || (pGestureNode->GetGestureTypeId() == pParams->m_typeId);

	if (!matches)
	{
		return true;
	}

	const Point nodePosWs = pParams->m_smoothed ? pGestureNode->GetSprungTargetPosWs() : pGestureNode->GetGoalTargetPosWs();

	if (pParams->m_valid)
	{
		pParams->m_targetPosWs = Lerp(pParams->m_targetPosWs, nodePosWs, combinedBlend);
	}
	else
	{
		pParams->m_targetPosWs = nodePosWs;
		pParams->m_valid = true;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("gesture-get-target-ws-from-snapshot", DcGestureGetTargetWsFromSnapshot)
{
	SCRIPT_ARG_ITERATOR(args, 3);
	const StringId64 targetTypeId = args.NextStringId();
	const AnimSnapshotNode* pNode = args.NextPointer<AnimSnapshotNode>();
	const AnimStateSnapshot* pStateSnapshot = args.NextPointer<AnimStateSnapshot>();
	const bool smoothedPoint = args.NextBoolean();

	VisitTargetPointParams params;
	params.m_typeId		 = targetTypeId;
	params.m_targetPosWs = kOrigin;
	params.m_smoothed	 = smoothedPoint;
	params.m_valid		 = false;

	pNode->VisitNodesOfKind(pStateSnapshot,
							SID("IGestureNode"),
							GestureNodeVisitFuncGetTargetPoint,
							(uintptr_t)&params);

	if (params.m_valid)
	{
		return ScriptValue(NDI_NEW(kAllocSingleGameFrame, kAlign16) Point(params.m_targetPosWs));
	}
	else
	{
		return ScriptValue(nullptr);
	}
}
