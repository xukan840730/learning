/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-node-weapon-ik-feather.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-node-library.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-plugin-context.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/anim/anim-state.h"
#include "ndlib/anim/armik.h"
#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/anim/ik/jacobian-ik.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/ik/two-bone-ik.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-skeleton.h"

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(8) WeaponIkFeatherAnimPluginData
{
	I32 m_sourceInstance = -1;

	U32 m_hand = kLeftArm;

	float m_handBlend  = 1.0f;
	float m_ikFade	   = 1.0f;
	float m_masterFade = 1.0f;

	I32 m_minIterations = -1;
	I32 m_maxIterations = 1;

	float m_ikRestorePercent = 1.0f;
	
	bool m_twoBone = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
ANIM_NODE_REGISTER_WITH_SNAPSHOT_FUNC(AnimSnapshotNodeWeaponIkFeather,
									  AnimSnapshotNodeUnary,
									  SID("anim-node-weapon-ik-feather"),
									  AnimSnapshotNodeWeaponIkFeather::SnapshotAnimNode);

FROM_ANIM_NODE_DEFINE(AnimSnapshotNodeWeaponIkFeather);

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSnapshotNodeWeaponIkFeather::AnimSnapshotNodeWeaponIkFeather(StringId64 typeId,
																 StringId64 dcTypeId,
																 SnapshotNodeHeap::Index nodeIndex)
	: ParentClass(typeId, dcTypeId, nodeIndex)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeWeaponIkFeather::SnapshotNode(AnimStateSnapshot* pSnapshot,
												   const DC::AnimNode* pNode,
												   const SnapshotAnimNodeTreeParams& params,
												   SnapshotAnimNodeTreeResults& results)
{
	ParentClass::SnapshotNode(pSnapshot, pNode, params, results);

	const DC::AnimNodeWeaponIkFeather* pDcNode = (const DC::AnimNodeWeaponIkFeather*)pNode;
	const DC::WeaponIkFeatherParams* pParams = ScriptManager::Lookup<DC::WeaponIkFeatherParams>(pDcNode->m_paramsId, nullptr);

	m_paramsId = pDcNode->m_paramsId;

	if (pParams)
	{
		m_params = *pParams;
	}
	else
	{
		m_params.m_domHand		 = DC::kHandIkHandLeft;
		m_params.m_handFunc		 = nullptr;
		m_params.m_handBlend	 = 0.5f;
		m_params.m_minIterations = 4;
		m_params.m_maxIterations = 18;
		m_params.m_ikRestorePercent = 0.9;
		m_params.m_ikFade	  = 1.0f;
		m_params.m_masterFade = 1.0f;
	}

	if (m_params.m_handFunc)
	{
		DC::AnimInfoCollection scriptInfoCollection(*params.m_pInfoCollection);
		scriptInfoCollection.m_actor	= scriptInfoCollection.m_actor;
		scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
		scriptInfoCollection.m_top		= scriptInfoCollection.m_top;

		const ScriptValue argv[] = { ScriptValue(&scriptInfoCollection) };

		const DC::ScriptLambda* pLambda = m_params.m_handFunc;

		VALIDATE_LAMBDA(pLambda, "hand-ik-func", m_animState.m_name.m_symbol);

		ScriptValue lambdaResult = ScriptManager::Eval(pLambda, SID("hand-ik-func"), ARRAY_COUNT(argv), argv);
		
		m_params.m_domHand = lambdaResult.m_int32;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeWeaponIkFeather::GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
														   AnimCmdList* pAnimCmdList,
														   I32F outputInstance,
														   const AnimCmdGenInfo* pCmdGenInfo) const
{
	ParentClass::GenerateAnimCommands(pSnapshot, pAnimCmdList, outputInstance, pCmdGenInfo);

	if (pCmdGenInfo->m_pContext->m_pAnimData->m_animLod >= DC::kAnimLodFar)
	{
		return;
	}

	GenerateCommands_PostBlend(pAnimCmdList, outputInstance, m_params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void AnimSnapshotNodeWeaponIkFeather::GenerateCommands_PostBlend(AnimCmdList* pAnimCmdList,
																 I32F outputInstance,
																 const DC::WeaponIkFeatherParams& params)
{
	WeaponIkFeatherAnimPluginData cmd;

	cmd.m_sourceInstance = outputInstance;

	switch (params.m_domHand)
	{
	case DC::kHandIkHandLeft:
		cmd.m_hand = kLeftArm;
		break;
	case DC::kHandIkHandRight:
		cmd.m_hand = kRightArm;
		break;
	default:
		cmd.m_hand = kLeftArm;
		break;
	}

	cmd.m_handBlend	 = params.m_handBlend;
	cmd.m_ikFade	 = params.m_ikFade;
	cmd.m_masterFade = params.m_masterFade;

	cmd.m_minIterations = params.m_minIterations;
	cmd.m_maxIterations = params.m_maxIterations;
	cmd.m_ikRestorePercent = params.m_ikRestorePercent;

	cmd.m_twoBone = params.m_twoBoneMode;

	pAnimCmdList->AddCmd_EvaluateAnimPhasePlugin(SID("weapon-ik-feather"), &cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float DetermineHandBlend(JointSet* pJointSet, U32F domHand)
{
	const I32F iLeftWrist  = pJointSet->FindJointOffset(SID("l_wrist"));
	const I32F iRightWrist = pJointSet->FindJointOffset(SID("r_wrist"));

	const I32F iLeftProp  = pJointSet->FindJointOffset(SID("l_hand_prop_attachment"));
	const I32F iRightProp = pJointSet->FindJointOffset(SID("r_hand_prop_attachment"));

	const Locator leftPropLocWs	 = pJointSet->GetJointLocWs(iLeftProp);
	const Locator rightPropLocWs = pJointSet->GetJointLocWs(iRightProp);

	const Locator leftWristLocWs = pJointSet->GetJointLocWs(iLeftWrist);
	const Locator rightWristLocWs = pJointSet->GetJointLocWs(iRightWrist);

	const Locator leftWristDelta  = leftPropLocWs.UntransformLocator(leftWristLocWs);
	const Locator rightWristDelta = rightPropLocWs.UntransformLocator(rightWristLocWs);

	// const Locator leftPropDelta	= leftWristLocWs.UntransformLocator(leftPropLocWs);
	// const Locator rightPropDelta = rightWristLocWs.UntransformLocator(rightPropLocWs);

	const Locator altLeftWristWs  = rightPropLocWs.TransformLocator(leftWristDelta);
	const Locator altRightWristWs = leftPropLocWs.TransformLocator(rightWristDelta);

	const float leftError  = Dist(altLeftWristWs.Pos(), leftWristLocWs.Pos());
	const float rightError = Dist(altRightWristWs.Pos(), rightWristLocWs.Pos());

	//g_prim.Draw(DebugCoordAxesLabeled(altLeftWristWs, StringBuilder<64>("alt-left-wrist : %0.3fm", leftError).c_str(), 0.1f, kPrimEnableHiddenLineAlpha, 1.0f, kColorWhite, 0.5f), kPrimDuration1FramePauseable);
	//g_prim.Draw(DebugCoordAxesLabeled(altRightWristWs, StringBuilder<64>("alt-right-wrist : %0.3fm", rightError).c_str(), 0.1f, kPrimEnableHiddenLineAlpha, 1.0f, kColorWhite, 0.5f), kPrimDuration1FramePauseable);

	const float param = (leftError > NDI_FLT_EPSILON) ? (Limit01(rightError / leftError) * 0.5f) : 0.0f;

	//g_prim.Draw(DebugString(Lerp(leftPropLocWs.Pos(), rightPropLocWs.Pos(), param) + Vector(0.0f, 0.1f, 0.0f), StringBuilder<64>("%f", param).c_str(), kColorWhite, 0.6f), kPrimDuration1FramePauseable);

	return param;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeWeaponIkFeather::AnimPluginCallback(StringId64 pluginId,
														 AnimPluginContext* pPluginContext,
														 const void* pData)
{
	if ((pPluginContext->m_pGroupContext->m_pSegmentContext->m_iSegment != 0)
		|| (pPluginContext->m_pGroupContext->m_iProcessingGroup != 0))
	{
		return;
	}

	const WeaponIkFeatherAnimPluginData* pPluginData = (const WeaponIkFeatherAnimPluginData*)pData;

	JointSet* pJointSet = pPluginContext->m_pContext->m_pPluginJointSet;

	if (!pJointSet || (pJointSet->GetNdGameObject() == nullptr))
	{
		return;
	}

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	ndanim::JointParams* pJointParamsLs = pPluginContext->GetJoints(pPluginData->m_sourceInstance);
	OrbisAnim::ValidBits* pValidBits	= pPluginContext->GetValidBits(pPluginData->m_sourceInstance);
	OrbisAnim::ValidBits* pValidBits2	= pPluginContext->GetBlendGroupJointValidBits(pPluginData->m_sourceInstance);

	const ndanim::JointHierarchy* pHierarchy = pPluginContext->m_pContext->m_pSkel->m_pAnimHierarchy;

	const ndanim::JointParams* pDefaultParamsLs = ndanim::GetDefaultJointPoseTable(pHierarchy);

	if (!pJointSet->ReadFromJointParams(pJointParamsLs,
										0, 
										OrbisAnim::kJointGroupSize,
										1.0f, 
										pValidBits, 
										pDefaultParamsLs))
	{
		return;
	}

	if (false)
	{
		pJointSet->DebugDrawJoints();
	}

	const I32F iLeftProp  = pJointSet->FindJointOffset(SID("l_hand_prop_attachment"));
	const I32F iRightProp = pJointSet->FindJointOffset(SID("r_hand_prop_attachment"));

	float handBlend = (pPluginData->m_hand == kLeftArm) ? pPluginData->m_handBlend : (1.0f - pPluginData->m_handBlend);

	if (TRUE_IN_FINAL_BUILD(!g_animOptions.m_procedural.m_weaponIkFeatherDisableAutoHandBlend))
	{
		handBlend = DetermineHandBlend(pJointSet, pPluginData->m_hand);
	}

	if (pPluginData->m_twoBone || FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_weaponIkFeatherForce2Bone))
	{
		const I32F iLeftWrist  = pJointSet->FindJointOffset(SID("l_wrist"));
		const I32F iRightWrist = pJointSet->FindJointOffset(SID("r_wrist"));

		const Locator leftPropLocWs	 = pJointSet->GetJointLocWs(iLeftProp);
		const Locator rightPropLocWs = pJointSet->GetJointLocWs(iRightProp);

		const Locator leftWristLocWs  = pJointSet->GetJointLocWs(iLeftWrist);
		const Locator rightWristLocWs = pJointSet->GetJointLocWs(iRightWrist);

		const Locator avgPropLocWs = Lerp(leftPropLocWs, rightPropLocWs, handBlend);
		const Locator avgWristLocWs = Lerp(leftWristLocWs, rightWristLocWs, handBlend);

		const Locator leftWristDelta  = leftPropLocWs.UntransformLocator(leftWristLocWs);
		const Locator rightWristDelta = rightPropLocWs.UntransformLocator(rightWristLocWs);

		const Locator leftTargWs  = avgPropLocWs.TransformLocator(leftWristDelta);
		const Locator rightTargWs = avgPropLocWs.TransformLocator(rightWristDelta);

		TwoBoneIkParams ikParams[kArmCount];
		ikParams[kLeftArm].m_finalGoalRot = leftTargWs.Rot();
		ikParams[kLeftArm].m_goalPos = leftTargWs.Pos();
		ikParams[kLeftArm].m_jointOffsets[0] = pJointSet->FindJointOffset(SID("l_shoulder"));
		ikParams[kLeftArm].m_jointOffsets[1] = pJointSet->FindJointOffset(SID("l_elbow"));
		ikParams[kLeftArm].m_jointOffsets[2] = iLeftWrist;
		ikParams[kLeftArm].m_objectSpace = false;
		ikParams[kLeftArm].m_pJointSet = pJointSet;
		ikParams[kLeftArm].m_tt = pPluginData->m_ikFade;

		ikParams[kRightArm].m_finalGoalRot = rightTargWs.Rot();
		ikParams[kRightArm].m_goalPos = rightTargWs.Pos();
		ikParams[kRightArm].m_jointOffsets[0] = pJointSet->FindJointOffset(SID("r_shoulder"));
		ikParams[kRightArm].m_jointOffsets[1] = pJointSet->FindJointOffset(SID("r_elbow"));
		ikParams[kRightArm].m_jointOffsets[2] = iRightWrist;
		ikParams[kRightArm].m_objectSpace = false;
		ikParams[kRightArm].m_pJointSet = pJointSet;
		ikParams[kRightArm].m_tt = pPluginData->m_ikFade;

		TwoBoneIkResults results[kArmCount];
		bool success[kArmCount];

		if (true)
		{
			success[kLeftArm]  = SolveTwoBoneIK(ikParams[kLeftArm], results[kLeftArm]);
			success[kRightArm] = SolveTwoBoneIK(ikParams[kRightArm], results[kRightArm]);

			//pJointSet->SetJointLocWs(iLeft, avgLocWs);
			//pJointSet->SetJointLocWs(iRight, avgLocWs);
		}

		if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_debugWeaponIkFeather))
		{
			//g_prim.Draw(DebugCoordAxesLabeled(leftTargWs, "left-targ", 1.0f, kPrimEnableHiddenLineAlpha, 1.0f, kColorWhite, 0.5f), kPrimDuration1FramePauseable);
			//g_prim.Draw(DebugCoordAxesLabeled(rightTargWs, "right-targ", 1.0f, kPrimEnableHiddenLineAlpha, 1.0f, kColorWhite, 0.5f), kPrimDuration1FramePauseable);
			
			g_prim.Draw(DebugCoordAxesLabeled(leftPropLocWs, "left-prop", 0.15f, kPrimEnableHiddenLineAlpha, 1.0f, kColorWhite, 0.5f), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCoordAxesLabeled(rightPropLocWs, "right-prop", 0.15f, kPrimEnableHiddenLineAlpha, 1.0f, kColorWhite, 0.5f), kPrimDuration1FramePauseable);

			StringBuilder<256> desc;
			desc.format("avg-prop @ %0.4f (ik: %0.4f)", pPluginData->m_masterFade, pPluginData->m_ikFade);
			g_prim.Draw(DebugCoordAxesLabeled(avgPropLocWs, desc.c_str(), 0.15f, kPrimEnableHiddenLineAlpha, 1.0f, kColorWhite, 0.5f), kPrimDuration1FramePauseable);

			//g_prim.Draw(DebugCoordAxesLabeled(leftWristLocWs, "left-wrist", 0.1f, kPrimEnableHiddenLineAlpha, 1.0f, kColorWhite, 0.5f), kPrimDuration1FramePauseable);
			//g_prim.Draw(DebugCoordAxesLabeled(rightWristLocWs, "right-wrist", 0.1f, kPrimEnableHiddenLineAlpha, 1.0f, kColorWhite, 0.5f), kPrimDuration1FramePauseable);
			//g_prim.Draw(DebugCoordAxesLabeled(avgWristLocWs, "avg-wrist", 0.1f, kPrimEnableHiddenLineAlpha, 1.0f, kColorWhite, 0.5f), kPrimDuration1FramePauseable);


			//pJointSet->DebugDrawJoints(true);
		}
		//const Locator invLeftWristDelta = Inverse(leftWristDelta);
		//g_prim.Draw(DebugCoordAxesLabeled(leftTargWs.TransformLocator(invLeftWristDelta), "left-targ-avg", 1.0f, kPrimEnableHiddenLineAlpha, 1.0f, kColorWhite, 0.5f), kPrimDuration1FramePauseable);
	}
	else
	{
		JacobianMap::EndEffectorDef effs[] =
		{
			JacobianMap::EndEffectorDef(SID("l_hand_prop_attachment"), IkGoal::kRotation),
			JacobianMap::EndEffectorDef(SID("l_hand_prop_attachment"), IkGoal::kPosition),
			JacobianMap::EndEffectorDef(SID("r_hand_prop_attachment"), IkGoal::kRotation),
			JacobianMap::EndEffectorDef(SID("r_hand_prop_attachment"), IkGoal::kPosition),
		};

		JacobianMap aJacobians[2];
		aJacobians[kLeftArm].Init(pJointSet, SID("spined"), 2, effs);
		aJacobians[kRightArm].Init(pJointSet, SID("spined"), 2, &effs[2]);

		JointSet* apJoints[2] = { pJointSet, pJointSet };
		JacobianMap* apJacobians[2] = { &aJacobians[0], &aJacobians[1] };

		//pJointSet->DebugDrawJoints(true);

		WeaponIKFeather::SolveWeaponFeatherIk(apJoints,
											  apJacobians,
											  pPluginData->m_hand,
											  handBlend,
											  pPluginData->m_ikFade,
											  pPluginData->m_maxIterations,
											  pPluginData->m_ikRestorePercent,
											  pPluginData->m_minIterations);
	}

	//const Locator postLeftLocWs	 = pJointSet->GetJointLocWs(iLeft);
	//const Locator postRightLocWs = pJointSet->GetJointLocWs(iRight);
	//const Locator leftLocLs	 = pJointSet->GetJointLocLs(iLeft);
	//const Locator rightLocLs = pJointSet->GetJointLocLs(iRight);

	//pJointSet->SetJointLocLs(iRight, leftLocLs);
	//pJointSet->SetJointLocLs(iLeft, rightLocLs);
	//pJointSet->UpdateAllJointLocsWs();

	//g_prim.Draw(DebugCoordAxesLabeled(postLeftLocWs, "left", 1.0f, kPrimEnableHiddenLineAlpha, 1.0f, kColorWhite, 0.5f), kPrimDuration1FramePauseable);
	//g_prim.Draw(DebugCoordAxesLabeled(postRightLocWs, "right", 1.0f, kPrimEnableHiddenLineAlpha, 1.0f, kColorWhite, 0.5f), kPrimDuration1FramePauseable);

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_disableWeaponIkFeather))
	{
		pJointSet->DiscardJointCache();
	}
	else
	{
		pJointSet->WriteJointParamsBlend(pPluginData->m_masterFade, pJointParamsLs, 0, OrbisAnim::kJointGroupSize, true);

		pJointSet->WriteJointValidBits(iLeftProp, 0, pValidBits);
		pJointSet->WriteJointValidBits(iLeftProp, 0, pValidBits2);
		pJointSet->WriteJointValidBits(iRightProp, 0, pValidBits);
		pJointSet->WriteJointValidBits(iRightProp, 0, pValidBits2);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeWeaponIkFeather::DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const
{
	Tab(pData->m_output, pData->m_depth);
	SetColor(pData->m_output, 0xFFFF00FF);
	PrintTo(pData->m_output, "Weapon IK Feather Node [params: '%s']\n", DevKitOnly_StringIdToString(m_paramsId));

	if (const AnimSnapshotNode* pChild = GetChild(pSnapshot))
	{
		++pData->m_depth;
		
		pChild->DebugPrint(pSnapshot, pData);
	
		--pData->m_depth;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
SnapshotNodeHeap::Index AnimSnapshotNodeWeaponIkFeather::SnapshotAnimNode(AnimStateSnapshot* pSnapshot,
																		  const DC::AnimNode* pNode,
																		  const SnapshotAnimNodeTreeParams& params,
																		  SnapshotAnimNodeTreeResults& results)
{
	ANIM_ASSERT(pNode->m_dcType == SID("anim-node-weapon-ik-feather"));

	SnapshotNodeHeap::Index returnNodeIndex = -1;

	const DC::AnimNodeWeaponIkFeather* pDcNode = static_cast<const DC::AnimNodeWeaponIkFeather*>(pNode);

	const StringId64 typeId = g_animNodeLibrary.LookupTypeIdFromDcType(pNode->m_dcType);

	if (typeId == INVALID_STRING_ID_64)
	{
		ANIM_ASSERTF(false, ("Bad AnimNode in tree! '%s'", DevKitOnly_StringIdToString(pNode->m_dcType)));
		return returnNodeIndex;
	}

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

	return returnNodeIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ForceLinkAnimSnapshotNodeWeaponIkFeather()
{
}
