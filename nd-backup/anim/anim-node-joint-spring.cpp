/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-node-joint-spring.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-plugin-context.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-skeleton.h"

#include <orbisanim/util.h>

/// --------------------------------------------------------------------------------------------------------------- ///
struct JointSpringPluginData
{
	AnimSnapshotNodeJointSpring* m_pSourceNode = nullptr;
	I32F m_sourceInstance = -1;
};

/// --------------------------------------------------------------------------------------------------------------- ///
ANIM_NODE_REGISTER_WITH_SNAPSHOT_FUNC(AnimSnapshotNodeJointSpring,
									  AnimSnapshotNodeUnary,
									  SID("anim-node-joint-spring"),
									  AnimSnapshotNodeJointSpring::SnapshotAnimNode);

FROM_ANIM_NODE_DEFINE(AnimSnapshotNodeJointSpring);

/// --------------------------------------------------------------------------------------------------------------- ///
static const AnimSnapshotNodeJointSpring* FindPreviousSpringInstance(const DC::AnimNodeJointSpring* pNewDcNode,
																	 const SnapshotAnimNodeTreeParams& params);

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSnapshotNodeJointSpring::AnimSnapshotNodeJointSpring(StringId64 typeId,
														 StringId64 dcTypeId,
														 SnapshotNodeHeap::Index nodeIndex)
	: ParentClass(typeId, dcTypeId, nodeIndex)
	, m_jointNameId(INVALID_STRING_ID_64)
	, m_jointIndex(-1)
	, m_springK(kLargeFloat)
	, m_dampingRatio(1.0f)
	, m_mode(DC::kJointSpringModeMax)
	, m_error(false)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeJointSpring::SnapshotNode(AnimStateSnapshot* pSnapshot,
											   const DC::AnimNode* pDcAnimNode,
											   const SnapshotAnimNodeTreeParams& params,
											   SnapshotAnimNodeTreeResults& results)
{
	ParentClass::SnapshotNode(pSnapshot, pDcAnimNode, params, results);

	const DC::AnimNodeJointSpring* pDcNode = static_cast<const DC::AnimNodeJointSpring*>(pDcAnimNode);

	m_jointNameId  = pDcNode->m_jointName;
	m_jointIndex   = params.m_pAnimData->FindJoint(pDcNode->m_jointName);
	m_springK	   = pDcNode->m_springK;
	m_mode		   = pDcNode->m_mode;
	m_dampingRatio = pDcNode->m_damping;
	m_error = false;

	if (FALSE_IN_FINAL_BUILD(m_jointIndex < 0))
	{
		MsgConErr("Joint Spring Node failed to find joint '%s'\n", DevKitOnly_StringIdToString(pDcNode->m_jointName));
	}

	if (const AnimSnapshotNodeJointSpring* pPrevSpringInst = FindPreviousSpringInstance(pDcNode, params))
	{
		m_firstFrame = false;
		m_goalRot	 = pPrevSpringInst->m_goalRot;
		m_sprungRot	 = pPrevSpringInst->m_sprungRot;
		m_rotTracker = pPrevSpringInst->m_rotTracker;
		m_error = pPrevSpringInst->m_error;
	}
	else
	{
		m_firstFrame = true;
		m_goalRot = m_sprungRot = kIdentity;
		m_rotTracker.Reset();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeJointSpring::StepNode(AnimStateSnapshot* pSnapshot,
										   float deltaTime,
										   const DC::AnimInfoCollection* pInfoCollection)
{
	ParentClass::StepNode(pSnapshot, deltaTime, pInfoCollection);

	if (!m_firstFrame)
	{
		m_sprungRot = m_rotTracker.Track(m_sprungRot, m_goalRot, deltaTime, m_springK, m_dampingRatio);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeJointSpring::GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
													   AnimCmdList* pAnimCmdList,
													   I32F outputInstance,
													   const AnimCmdGenInfo* pCmdGenInfo) const
{
	ParentClass::GenerateAnimCommands(pSnapshot, pAnimCmdList, outputInstance, pCmdGenInfo);
	
 	if (!FALSE_IN_FINAL_BUILD(g_animOptions.m_jointSpringNodes.m_disable))
	{
		JointSpringPluginData cmd;
		cmd.m_pSourceNode = const_cast<AnimSnapshotNodeJointSpring*>(this);
		cmd.m_sourceInstance = outputInstance;

		pAnimCmdList->AddCmd_EvaluateAnimPhasePlugin(SID("joint-spring-node"), &cmd);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeJointSpring::DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const
{
	Tab(pData->m_output, pData->m_depth);
	SetColor(pData->m_output, m_error ? kColorRed.ToAbgr8() : 0xFFFF00FF);
	
	const Vector sprungDirOs = GetLocalZ(m_sprungRot);
	const Vector goalDirOs = GetLocalZ(m_goalRot);
	const float angleDiffDeg = RADIANS_TO_DEGREES(SafeAcos(Dot(sprungDirOs, goalDirOs)));

	PrintTo(pData->m_output,
			"Joint Spring Node ['%s' @ %0.1f in %s] Angle Diff: %0.1fdeg\n",
			DevKitOnly_StringIdToString(m_jointNameId),
			m_springK,
			DC::GetJointSpringModeName(m_mode),
			angleDiffDeg);
	pData->m_depth++;
	GetChild(pSnapshot)->DebugPrint(pSnapshot, pData);
	pData->m_depth--;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeJointSpring::IsContiguousWith(const DC::AnimNodeJointSpring* pDcNode) const
{
	if (!pDcNode)
		return false;

	if (pDcNode->m_jointName != m_jointNameId)
		return false;

	if (pDcNode->m_mode != m_mode)
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
SnapshotNodeHeap::Index AnimSnapshotNodeJointSpring::SnapshotAnimNode(AnimStateSnapshot* pSnapshot,
																	  const DC::AnimNode* pNode,
																	  const SnapshotAnimNodeTreeParams& params,
																	  SnapshotAnimNodeTreeResults& results)
{
	ANIM_ASSERT(pNode->m_dcType == SID("anim-node-joint-spring"));

	const DC::AnimNodeJointSpring* pDcNode = static_cast<const DC::AnimNodeJointSpring*>(pNode);

	SnapshotNodeHeap::Index returnNodeIndex = -1;

	const I32F jointIndex = params.m_pAnimData->FindJoint(pDcNode->m_jointName);

	if ((jointIndex >= 0) && (pDcNode->m_springK > 0.0f))
	{
		const StringId64 typeId = g_animNodeLibrary.LookupTypeIdFromDcType(pNode->m_dcType);

		ANIM_ASSERT(typeId != INVALID_STRING_ID_64);

		if (AnimSnapshotNode* pNewNode = pSnapshot->m_pSnapshotHeap->AllocateNode(typeId, &returnNodeIndex))
		{
			pNewNode->SnapshotNode(pSnapshot, pNode, params, results);
		}
		else
		{
			MsgAnimErr("[%s] Ran out of snapshot memory creating new snapshot node '%s' for state '%s'\n",
					   DevKitOnly_StringIdToString(params.m_pAnimData->m_hProcess.GetUserId()),
					   DevKitOnly_StringIdToString(pNode->m_dcType),
					   pSnapshot->m_animState.m_name.m_string.GetString());

			returnNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
		}
	}
	else
	{
		returnNodeIndex = AnimStateSnapshot::SnapshotAnimNodeTree(pSnapshot, pDcNode->m_child, params, results);
	}

	return returnNodeIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Locator ReadJointLoc(const NdGameObject* pGo, JointSet* pJointSet, I32F jointIndex, DC::JointSpringMode mode)
{
	ANIM_ASSERT(pJointSet);

	Locator loc = kIdentity;

	switch (mode)
	{
	case DC::kJointSpringModeLocalSpace:
		loc = pJointSet->GetJointLocLsIndex(jointIndex);
		break;

	case DC::kJointSpringModeObjectSpace:
		loc = pJointSet->GetJointLocOsIndex(jointIndex);
		break;

	case DC::kJointSpringModeParentSpace:
		{
			const Locator locWs = pJointSet->GetJointLocWsIndex(jointIndex);
			loc = pGo ? pGo->GetParentSpace().UntransformLocator(locWs) : locWs;
		}
		break;

	case DC::kJointSpringModeWorldSpace:
		loc = pJointSet->GetJointLocWsIndex(jointIndex);
		break;
	}

	return loc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void SetJointLoc(const NdGameObject* pGo,
						JointSet* pJointSet,
						I32F jointIndex,
						const Locator& loc,
						DC::JointSpringMode mode)
{
	ANIM_ASSERT(pJointSet);

	switch (mode)
	{
	case DC::kJointSpringModeLocalSpace:
		pJointSet->SetJointLocLsIndex(jointIndex, loc);
		break;

	case DC::kJointSpringModeObjectSpace:
		pJointSet->SetJointLocOsIndex(jointIndex, loc);
		break;

	case DC::kJointSpringModeParentSpace:
		{
			const Locator locWs = pGo ? pGo->GetParentSpace().TransformLocator(loc) : loc;
			pJointSet->SetJointLocWsIndex(jointIndex, locWs);
		}
		break;

	case DC::kJointSpringModeWorldSpace:
		pJointSet->SetJointLocWsIndex(jointIndex, loc);
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void AnimSnapshotNodeJointSpring::AnimPluginCallback(AnimPluginContext* pPluginContext, const void* pData)
{
	if ((pPluginContext->m_pGroupContext->m_pSegmentContext->m_iSegment != 0)
		|| (pPluginContext->m_pGroupContext->m_iProcessingGroup != 0))
	{
		return;
	}

	const JointSpringPluginData* pPluginData = static_cast<const JointSpringPluginData*>(pData);
	AnimSnapshotNodeJointSpring* pSourceNode = pPluginData->m_pSourceNode;

	JointSet* pJointSet = pPluginContext->m_pContext->m_pPluginJointSet;

	if (!pJointSet)
		return;

	if (pSourceNode->m_jointIndex < 0)
		return;

	if (pJointSet->GetJointOffset(pSourceNode->m_jointIndex) < 0)
		return;

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	const NdGameObject* pGo = pJointSet->GetNdGameObject();

	ndanim::JointParams* pJointParamsLs = pPluginContext->GetJoints(pPluginData->m_sourceInstance);
	OrbisAnim::ValidBits* pValidBits	= pPluginContext->GetValidBits(pPluginData->m_sourceInstance);
	OrbisAnim::ValidBits* pValidBits2	= pPluginContext->GetBlendGroupJointValidBits(pPluginData->m_sourceInstance);

	const ndanim::JointHierarchy* pHier			= pPluginContext->m_pContext->m_pSkel->m_pAnimHierarchy;
	const ndanim::JointParams* pDefaultParamsLs = ndanim::GetDefaultJointPoseTable(pHier);

	const U32F numJoints = OrbisAnim::AnimHierarchy_GetNumAnimatedJointsInGroup(pHier, 0);

	const bool res = pJointSet->ReadFromJointParams(pJointParamsLs,
													0,
													OrbisAnim::kJointGroupSize,
													1.0f,
													pValidBits,
													pDefaultParamsLs);

	pSourceNode->m_error = !res;

	if (!res)
	{
		return;
	}

	const Locator animLoc = ReadJointLoc(pGo, pJointSet, pSourceNode->m_jointIndex, pSourceNode->m_mode);

	pSourceNode->m_goalRot = animLoc.Rot();

	if (pSourceNode->m_firstFrame)
	{
		pSourceNode->m_firstFrame = false;
		pSourceNode->m_sprungRot = animLoc.Rot();
	}
	else
	{
		const Locator sprungLoc = Locator(animLoc.Pos(), pSourceNode->m_sprungRot);

		SetJointLoc(pGo, pJointSet, pSourceNode->m_jointIndex, sprungLoc, pSourceNode->m_mode);
	}

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_jointSpringNodes.m_debugDraw))
	{
		Quat sprungRotWs;
		Locator animLocWs;

		switch (pSourceNode->m_mode)
		{
		case DC::kJointSpringModeLocalSpace:
			{
				const Locator parentLocWs = pJointSet->GetParentLocWs(pSourceNode->m_jointIndex);
				sprungRotWs = parentLocWs.Rot() * pSourceNode->m_sprungRot;
				animLocWs = parentLocWs.TransformLocator(animLoc);
			}
			break;

		case DC::kJointSpringModeParentSpace:
			sprungRotWs = pGo ? pGo->GetParentSpace().Rot() * pSourceNode->m_sprungRot : pSourceNode->m_sprungRot;
			animLocWs = pGo ? pGo->GetParentSpace().TransformLocator(animLoc) : animLoc;
			break;

		case DC::kJointSpringModeWorldSpace:
			sprungRotWs = pSourceNode->m_sprungRot;
			animLocWs = animLoc;
			break;

		case DC::kJointSpringModeObjectSpace:
		default:
			sprungRotWs = pGo ? (pGo->GetLocator().Rot() * pSourceNode->m_sprungRot) : pSourceNode->m_sprungRot;
			animLocWs = pGo ? (pGo->GetLocator().TransformLocator(animLoc)) : animLoc;
			break;
		}

		const Point drawPosWs = animLocWs.Pos(); 
		const Vector animDirWs = GetLocalZ(animLocWs.Rot());
		const Vector sprungDirWs = GetLocalZ(sprungRotWs);

		g_prim.Draw(DebugArrow(drawPosWs, animDirWs, kColorRedTrans), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugString(drawPosWs + animDirWs,
								StringBuilder<256>("%s anim", DevKitOnly_StringIdToString(pSourceNode->m_jointNameId))
									.c_str(),
								kColorRedTrans,
								0.5f),
					kPrimDuration1FramePauseable);

		g_prim.Draw(DebugArrow(drawPosWs, sprungDirWs, kColorGreen), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugString(drawPosWs + sprungDirWs,
								StringBuilder<256>("%s sprung", DevKitOnly_StringIdToString(pSourceNode->m_jointNameId))
									.c_str(),
								kColorGreen,
								0.5f),
					kPrimDuration1FramePauseable);
	}

	pJointSet->WriteJointParamsBlend(1.0f, pJointParamsLs, 0, OrbisAnim::kJointGroupSize);

	const I32F jointOffset = pJointSet->GetJointOffset(pSourceNode->m_jointIndex);
	pJointSet->WriteJointValidBits(jointOffset, 0, pValidBits, false);
	pJointSet->WriteJointValidBits(jointOffset, 0, pValidBits2, false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct JointSpringNodeKindFuncData
{
	const DC::AnimNodeJointSpring* m_pNewDcNode = nullptr;
	const AnimSnapshotNodeJointSpring* m_pFoundNode = nullptr;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool JointSpringNodeKindFunc(const AnimStateSnapshot* pSnapshot,
									const AnimSnapshotNode* pNode,
									const AnimSnapshotNodeBlend* pParentBlendNode,
									float combinedBlend,
									uintptr_t userData)
{
	if (!pNode || !pNode->IsKindOf(g_type_AnimSnapshotNodeUnary))
		return true;

	JointSpringNodeKindFuncData& data = *(JointSpringNodeKindFuncData*)userData;

	// keep going as long as we haven't found one
	if (data.m_pFoundNode)
		return false;

	const AnimSnapshotNodeJointSpring* pJointSpringNode = (const AnimSnapshotNodeJointSpring*)pNode;

	if (pJointSpringNode->IsContiguousWith(data.m_pNewDcNode))
	{
		data.m_pFoundNode = pJointSpringNode;
	}

	return data.m_pFoundNode == nullptr; 
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const AnimSnapshotNodeJointSpring* FindPreviousSpringInstance(const DC::AnimNodeJointSpring* pNewDcNode,
																	 const SnapshotAnimNodeTreeParams& params)
{
	const AnimStateInstance* pPrevInst = params.m_pFadeToStateParams ? params.m_pFadeToStateParams->m_pPrevInstance
																	 : nullptr;
	if (!pPrevInst)
		return nullptr;

	JointSpringNodeKindFuncData data;
	data.m_pNewDcNode = pNewDcNode;

	if (const AnimSnapshotNode* pRootNode = pPrevInst->GetRootSnapshotNode())
	{
		pRootNode->VisitNodesOfKind(&pPrevInst->GetAnimStateSnapshot(),
									SID("AnimSnapshotNodeJointSpring"),
									JointSpringNodeKindFunc,
									(uintptr_t)&data);
	}

	return data.m_pFoundNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ForceLinkAnimSnapshotNodeJointSpring() {}
