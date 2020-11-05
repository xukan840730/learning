/*
 * Copyright (c) 2014 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-node-ik.h"

#include "corelib/memory/relocate.h"
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
#include "ndlib/anim/ik/jacobian-ik.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/profiling/profile-cpu-categories.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/level/art-item-skeleton.h"

/// --------------------------------------------------------------------------------------------------------------- ///
ANIM_NODE_REGISTER_WITH_SNAPSHOT_FUNC(AnimSnapshotNodeIK,
									  AnimSnapshotNodeUnary,
									  SID("anim-node-ik"),
									  AnimSnapshotNodeIK::SnapshotAnimNode);

FROM_ANIM_NODE_DEFINE(AnimSnapshotNodeIK);

/// --------------------------------------------------------------------------------------------------------------- ///
static IkGoal::LookTargetAxis LookTargetAxisFromDc(const DC::IkGoalAxis axis)
{
	switch (axis)
	{
	// case DC::kIkGoalAxisX: return IkGoal::kLookTargetAxisX;
	// case DC::kIkGoalAxisNegX: return IkGoal::kLookTargetAxisXNeg;
	case DC::kIkGoalAxisY:
		return IkGoal::kLookTargetAxisY;
	case DC::kIkGoalAxisNegY:
		return IkGoal::kLookTargetAxisYNeg;
	case DC::kIkGoalAxisZ:
		return IkGoal::kLookTargetAxisZ;
	case DC::kIkGoalAxisNegZ:
		return IkGoal::kLookTargetAxisZNeg;
	default:
		ANIM_ASSERT(false);
		break;
	}
	return IkGoal::kLookTargetAxisY;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static IkGoal::GoalType GoalTypeFromDC(const DC::IkGoalType type)
{
	switch (type)
	{
	case DC::kIkGoalTypeLookDir:
		return IkGoal::kLookDir;
	case DC::kIkGoalTypeLookTarget:
		return IkGoal::kLookTarget;
	case DC::kIkGoalTypeRotation:
		return IkGoal::kRotation;
	case DC::kIkGoalTypePosition:
		return IkGoal::kPosition;
	default:
		return IkGoal::kGoalNone;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const Point MirrorPoint(Point_arg p)
{
	return Point(-p.X(), p.Y(), p.Z());
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const Vector MirrorVector(Vector_arg v)
{
	return Vector(-v.X(), v.Y(), v.Z());
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const Quat MirrorRot(Quat_arg q)
{
	return Quat(-q.X(), q.Y(), q.Z(), -q.W());
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Locator ApplyMirror(const Locator& space, const Locator& loc, bool mirrored)
{
	Locator ls = space.UntransformLocator(loc);
	if (mirrored)
	{
		ls.SetTranslation(MirrorPoint(ls.Pos()));
		ls.SetRotation(MirrorRot(ls.Rot()));
	}
	return space.TransformLocator(ls);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Point ApplyMirror(const Locator& space, Point_arg p, bool mirrored)
{
	Point ls = space.UntransformPoint(p);
	if (mirrored)
	{
		ls = MirrorPoint(p);
	}
	return space.TransformPoint(ls);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Vector ApplyMirror(const Locator& space, Vector_arg v, bool mirrored)
{
	Vector ls = space.UntransformVector(v);
	if (mirrored)
	{
		ls = MirrorVector(v);
	}
	return space.TransformVector(ls);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Quat ApplyMirror(const Locator& space, Quat_arg q, bool mirrored)
{
	Quat ls = Conjugate(space.Rot()) * q;
	if (mirrored)
	{
		ls = MirrorRot(ls);
	}
	return space.Rot() * ls;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void IkGoalFromDc(const DCIkGoalWrapper* pGoal,
						 const Locator& alignWs,
						 const Locator& jointLocWs,
						 const bool mirrored,
						 IkGoal& outGoalWs)
{
	const DC::IkGoalSpace space = pGoal->m_posGoal.m_space;

	switch (pGoal->m_posGoal.m_typeId.GetValue())
	{
	case SID_VAL("ik-goal-position"):
		if (space == DC::kIkGoalSpaceObject)
		{
			const Point goalPosOs = ApplyMirror(kIdentity, pGoal->m_posGoal.m_pos, mirrored);
			const Point goalPosWs = alignWs.TransformPoint(goalPosOs);
			outGoalWs.SetGoalPosition(goalPosWs); 
		}
		else if (space == DC::kIkGoalSpaceWorld)
		{
			const Point goalPosWs = ApplyMirror(alignWs, pGoal->m_posGoal.m_pos, mirrored);
			outGoalWs.SetGoalPosition(goalPosWs);
		}
		else if (space == DC::kIkGoalSpaceWorldOffset)
		{
			const Point goalPosWs = ApplyMirror(alignWs,
												pGoal->m_posGoal.m_pos + AsVector(jointLocWs.GetTranslation()),
												mirrored);
			outGoalWs.SetGoalPosition(goalPosWs);
		}
		break;

	case SID_VAL("ik-goal-rotation"): 
		if (space == DC::kIkGoalSpaceObject)
		{
			const Quat goalRotOs = ApplyMirror(kIdentity, pGoal->m_rotGoal.m_rot, mirrored);
			const Quat goalRotWs = alignWs.Rot() * goalRotOs;
			outGoalWs.SetGoalRotation(goalRotWs);
		}
		else if (space == DC::kIkGoalSpaceWorld)
		{
			const Quat goalRotWs = ApplyMirror(alignWs, pGoal->m_rotGoal.m_rot, mirrored);
			outGoalWs.SetGoalRotation(goalRotWs);
		}
		else if (space == DC::kIkGoalSpaceWorldOffset)
		{
			const Quat goalRotWs = ApplyMirror(alignWs, pGoal->m_rotGoal.m_rot * jointLocWs.GetRotation(), mirrored);
			outGoalWs.SetGoalRotation(goalRotWs);
		}
		break;

	case SID_VAL("ik-goal-look-at"):
		if (space == DC::kIkGoalSpaceObject)
		{
			const Point goalPosOs = ApplyMirror(kIdentity, pGoal->m_lookAtGoal.m_pos, mirrored);
			const Point goalPosWs = alignWs.TransformPoint(goalPosOs);
			outGoalWs.SetGoalLookTarget(goalPosWs, LookTargetAxisFromDc(pGoal->m_lookAtGoal.m_axis), kUnitYAxis);
		}
		else
		{
			const Point goalPosWs = ApplyMirror(alignWs, pGoal->m_lookAtGoal.m_pos, mirrored);
			outGoalWs.SetGoalLookTarget(goalPosWs, LookTargetAxisFromDc(pGoal->m_lookAtGoal.m_axis), kUnitYAxis);
		}
		break;

	case SID_VAL("ik-goal-look-dir"): 
		if (space == DC::kIkGoalSpaceObject)
		{
			const Vector goalDirOs = ApplyMirror(kIdentity, pGoal->m_lookDirGoal.m_targetDir, mirrored);
			const Vector goalDirWs = alignWs.TransformVector(goalDirOs);
			outGoalWs.SetGoalLookDir(goalDirWs, pGoal->m_lookDirGoal.m_lookAxis);
		}
		else
		{
			const Vector goalDirWs = ApplyMirror(alignWs, pGoal->m_lookDirGoal.m_targetDir, mirrored);
			outGoalWs.SetGoalLookDir(goalDirWs, pGoal->m_lookDirGoal.m_lookAxis);
		}
		break;

	default:
		ANIM_ASSERTF(false, ("Unknown ik goal type '%s'", DevKitOnly_StringIdToString(pGoal->m_posGoal.m_typeId)));
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct IkPluginParams
{
	I32 m_instanceIndex;
	JacobianIkInstance* m_pJacobianInstance;
	F32 m_blend;
	bool m_flipped;
	const ListArray<DCIkGoalWrapper>* m_pDcGoals;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeIK::AnimPluginCallback(AnimPluginContext* pPluginContext, const void* pData)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_disableAnimNodeIk))
	{
		return;
	}

	if ((pPluginContext->m_pGroupContext->m_pSegmentContext->m_iSegment != 0)
		|| (pPluginContext->m_pGroupContext->m_iProcessingGroup != 0))
	{
		return;
	}

	const I32 numJoints = OrbisAnim::kJointGroupSize;

	IkPluginParams* pParams = reinterpret_cast<IkPluginParams*>(const_cast<void*>(pData));
	JacobianIkInstance& ik = *pParams->m_pJacobianInstance;

	JacobianMap* pJacobianMap = ik.m_pJacobianMap;

	if (!pJacobianMap->m_endEffectorEntries)
	{
		return;
	}

	JointSet* pJointSet = pPluginContext->m_pContext->m_pPluginJointSet;

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	ndanim::JointParams* pAnimatedJointsLs = pPluginContext->GetJoints(pParams->m_instanceIndex);
	OrbisAnim::ValidBits* pValidBits	   = pPluginContext->GetValidBits(pParams->m_instanceIndex);
	OrbisAnim::ValidBits* pValidBits2	   = pPluginContext->GetBlendGroupJointValidBits(pParams->m_instanceIndex);
	const ndanim::JointHierarchy* pJointHier	= pPluginContext->m_pContext->m_pSkel->m_pAnimHierarchy;
	const ndanim::JointParams* pDefaultParamsLs = ndanim::GetDefaultJointPoseTable(pJointHier);

	if (!pJointSet->ReadFromJointParams(pAnimatedJointsLs, 0, numJoints, 1.0f, pValidBits, pDefaultParamsLs))
	{
		pJointSet->DiscardJointCache();
		return;
	}
		
	pJointSet->UpdateAllJointLocsWs();
		
	ik.m_pJoints = pJointSet;
	ik.m_pConstraints = pJointSet->GetJointConstraints();
	ik.m_errTolerance = 0.001f;
	ik.m_restoreFactor = 0.8f;
	ik.m_updateAllJointLocs = false; // already did this up above, for world-offset goal types		

	const Locator alignWs = Locator(*pPluginContext->m_pContext->m_pObjXform);

	U32F numValid = 0;

	//Convert DC goals to ik goals
	for (int iGoal = 0; iGoal < pParams->m_pDcGoals->Size(); iGoal++)
	{
		const DCIkGoalWrapper& goalWrapper = pParams->m_pDcGoals->at(iGoal);

		if (goalWrapper.m_valid)
		{
			Locator wsJointLoc(kIdentity);
			if (goalWrapper.m_posGoal.m_space == DC::kIkGoalSpaceWorldOffset)
			{
				wsJointLoc = alignWs.TransformLocator(pJointSet->GetJointLocOs(pJacobianMap->m_endEffectorEntries[iGoal].endEffectorOffset));
			}

			pJacobianMap->m_endEffectorEntries[iGoal].endEffectEnabled = true;
			IkGoalFromDc(&goalWrapper, alignWs, wsJointLoc, pParams->m_flipped, ik.m_goal[iGoal]);
			
			++numValid;
		}
		else
		{
			pJacobianMap->m_endEffectorEntries[iGoal].endEffectEnabled = false;
			ik.m_goal[iGoal].SetStrength(0.0f);
		}
	}

	if (numValid == 0)
	{
		pJointSet->DiscardJointCache();
		return;
	}

	SolveJacobianIK(&ik);

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_debugAnimNodeIk))
	{
		if (g_animOptions.m_procedural.m_debugAnimNodeIkJoints)
		{
			pJointSet->DebugDrawJoints();
		}

		for (int iGoal = 0; iGoal < pParams->m_pDcGoals->Size(); iGoal++)
		{
			if (ik.m_goal[iGoal].GetGoalType() == IkGoal::kLookTarget)
			{
				const U32F endEffOffset = pJacobianMap->m_endEffectorEntries[iGoal].endEffectorOffset;
				const Locator endEffLocOffset = pJacobianMap->m_endEffectorEntries[iGoal].endEffectJointOffset;
				const Locator lookAtLocWs = pJointSet->GetJointLocWs(endEffOffset).TransformLocator(endEffLocOffset);

				g_prim.Draw(DebugArrow(lookAtLocWs.Pos(), GetLocalZ(lookAtLocWs.Rot()), kColorMagenta), kPrimDuration1FramePauseable);
					
				Point targetPosWs;
				IkGoal::LookTargetAxis lookAxis;
				Vector upVecWs;
				ik.m_goal[iGoal].GetGoalLookTarget(&targetPosWs, &lookAxis, &upVecWs);

				g_prim.Draw(DebugCross(targetPosWs, 0.2f), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugLine(lookAtLocWs.Pos(), targetPosWs - lookAtLocWs.Pos(), kColorRed), kPrimDuration1FramePauseable);
			}
		}
	}

	pJointSet->WriteJointParamsBlend(pParams->m_blend, pAnimatedJointsLs, 0, numJoints);

	for (int iGoal = 0; iGoal < pParams->m_pDcGoals->Size(); iGoal++)
	{
		const U32F endEffOffset = pJacobianMap->m_endEffectorEntries[iGoal].endEffectorOffset;

		pJointSet->WriteJointValidBits(endEffOffset, 0, pValidBits);
		pJointSet->WriteJointValidBits(endEffOffset, 0, pValidBits2);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeIK::GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
											  AnimCmdList* pAnimCmdList,
											  I32F outputInstance,
											  const AnimCmdGenInfo* pCmdGenInfo) const
{
	ParentClass::GenerateAnimCommands(pSnapshot, pAnimCmdList, outputInstance, pCmdGenInfo);

	const FgAnimData* pAnimData = pCmdGenInfo->m_pContext->m_pAnimData;

	if (!pAnimData->m_animateSkelHandle.ToArtItem()
		|| (pAnimData->m_animateSkelHandle.ToArtItem() != pAnimData->m_curSkelHandle.ToArtItem()))
	{
		return;
	}

	if (m_pJocobianMap && (m_blend > 0.0f))
	{
		IkPluginParams params;
		params.m_instanceIndex = outputInstance;
		params.m_pJacobianInstance = m_pIkInstance;
		params.m_pJacobianInstance->m_pJacobianMap = m_pJocobianMap;
		params.m_blend = m_blend;
		params.m_flipped = m_flipped;
		params.m_pDcGoals = &m_goals;

		pAnimCmdList->AddCmd_EvaluateAnimPhasePlugin(SID("ik-node"), &params);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EndEffectorFromDC(const DC::IkEndEffectorDef* pEndEffDef, JacobianMap::EndEffectorDef& outEndEffector )
{
	outEndEffector.m_jointId = pEndEffDef->m_jointId;
	outEndEffector.m_goalType =  GoalTypeFromDC(pEndEffDef->m_type);
	outEndEffector.m_enabled = true;
	outEndEffector.m_jointOffset = pEndEffDef->m_offset;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeIK::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	DeepRelocatePointer(m_pJocobianMap, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pIkInstance, deltaPos, lowerBound, upperBound);
	m_goals.Relocate(deltaPos, lowerBound, upperBound);
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeIK::RefreshPhasesAndBlends(AnimStateSnapshot* pSnapshot,
												float statePhase,
												bool topTrackInstance,
												const DC::AnimInfoCollection* pInfoCollection)
{
	bool result = ParentClass::RefreshPhasesAndBlends(pSnapshot, statePhase, topTrackInstance, pInfoCollection);

	RefreshGoalsAndBlend(pSnapshot, statePhase, pInfoCollection);

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeIK::RefreshGoalsAndBlend(AnimStateSnapshot* pSnapshot,
											  float statePhase,
											  const DC::AnimInfoCollection* pInfoCollection)
{
	if (!m_pIkInstance)
	{
		return;
	}

	DC::AnimInfoCollection scriptInfoCollection(*pInfoCollection);
	scriptInfoCollection.m_actor	= scriptInfoCollection.m_actor;
	scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
	scriptInfoCollection.m_top		= scriptInfoCollection.m_top;

	const AnimSnapshotNode* pChild = GetChild(pSnapshot);

	const ScriptValue argv[] = { ScriptValue(&scriptInfoCollection),
								 ScriptValue(statePhase),
								 ScriptValue(pChild),
								 ScriptValue(pSnapshot) };

	if (m_pGoalFuncs)
	{
		int iGoal = 0;

		for (const DC::ScriptLambda* pLambda : *m_pGoalFuncs)
		{
			VALIDATE_LAMBDA(pLambda, "ik-goal-func", m_animState.m_name.m_symbol);
			ScriptValue lambdaResult = ScriptManager::Eval(pLambda, SID("ik-goal-func"), ARRAY_COUNT(argv), argv);
			const DC::IkGoal* pGoal = reinterpret_cast<const DC::IkGoal*>(lambdaResult.m_pointer);

			if (pGoal)
			{
				m_goals[iGoal].m_valid = true;

				switch (pGoal->m_typeId.GetValue())
				{
				case SID_VAL("ik-goal-position"):
					m_goals[iGoal].m_posGoal = *static_cast<const DC::IkGoalPosition*>(pGoal);
					break;
				case SID_VAL("ik-goal-rotation"):
					m_goals[iGoal].m_rotGoal = *static_cast<const DC::IkGoalRotation*>(pGoal);
					break;
				case SID_VAL("ik-goal-look-at"):
					m_goals[iGoal].m_lookAtGoal = *static_cast<const DC::IkGoalLookAt*>(pGoal);
					break;
				case SID_VAL("ik-goal-look-dir"):
					m_goals[iGoal].m_lookDirGoal = *static_cast<const DC::IkGoalLookDir*>(pGoal);
					break;
				default:
					ANIM_ASSERT(false);
					break;
				}
			}
			else
			{
				m_goals[iGoal].m_valid = false;
			}

			iGoal++;
		}
	}

	if (m_pBlendFunc)
	{
		VALIDATE_LAMBDA(m_pBlendFunc, "ik-blend-func", m_animState.m_name.m_symbol);
		ScriptValue lambdaResult = ScriptManager::Eval(m_pBlendFunc, SID("ik-blend-func"), ARRAY_COUNT(argv), argv);
		m_blend = lambdaResult.m_float;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeIK::DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const
{
	Tab(pData->m_output, pData->m_depth);
	SetColor(pData->m_output, 0xFFFF00FF);
	PrintTo(pData->m_output, "Ik Node [%s]\n", DevKitOnly_StringIdToString(m_configName));
	pData->m_depth++;
	GetChild(pSnapshot)->DebugPrint(pSnapshot, pData);
	pData->m_depth--;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SnapshotNodeHeap::Index AnimSnapshotNodeIK::SnapshotAnimNode(AnimStateSnapshot* pSnapshot,
															 const DC::AnimNode* pNode,
															 const SnapshotAnimNodeTreeParams& params,
															 SnapshotAnimNodeTreeResults& results)
{
	ANIM_ASSERT(pNode->m_dcType == SID("anim-node-ik"));

	DC::AnimInfoCollection scriptInfoCollection(*params.m_pInfoCollection);
	scriptInfoCollection.m_actor = scriptInfoCollection.m_actor;
	scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
	scriptInfoCollection.m_top = scriptInfoCollection.m_top;

	const ScriptValue argv[] = { ScriptValue(&scriptInfoCollection) };

	const DC::AnimNodeIk* pIkNode = static_cast<const DC::AnimNodeIk*>(pNode);
	const DC::IkNodeConfig* pConfig = nullptr;

	if (pIkNode->m_configFunc)
	{
		const DC::ScriptLambda* pLambda = pIkNode->m_configFunc;
		VALIDATE_LAMBDA(pLambda, "ik-config-func", m_animState.m_name.m_symbol);
		ScriptValue lambdaResult = ScriptManager::Eval(pLambda, SID("ik-config-func"), ARRAY_COUNT(argv), argv);
		pConfig = reinterpret_cast<const DC::IkNodeConfig*>(lambdaResult.m_pointer);
	}
	else if (pIkNode->m_config)
	{
		pConfig = pIkNode->m_config;
	}
	else
	{
		pConfig = ScriptManager::Lookup<DC::IkNodeConfig>(pIkNode->m_configName, nullptr);
	}

	SnapshotNodeHeap::Index returnNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;

	if (pConfig && pConfig->m_endEffectors && pConfig->m_endEffectors->GetSize() > 0)
	{
		const StringId64 typeId = g_animNodeLibrary.LookupTypeIdFromDcType(pNode->m_dcType);

		if (typeId == INVALID_STRING_ID_64)
		{
			ANIM_ASSERTF(false, ("Bad AnimNode in tree! '%s'", DevKitOnly_StringIdToString(pNode->m_dcType)));
		}
		else if (AnimSnapshotNode* pNewNode = pSnapshot->m_pSnapshotHeap->AllocateNode(typeId, &returnNodeIndex))
		{
			pNewNode->SnapshotNode(pSnapshot, pNode, params, results);

			AnimSnapshotNodeIK* pNewIk = AnimSnapshotNodeIK::FromAnimNode(pNewNode);

			if (pNewIk)
			{
				pNewIk->InitIk(pConfig, scriptInfoCollection, params);
			}
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
		returnNodeIndex = AnimStateSnapshot::SnapshotAnimNodeTree(pSnapshot, pIkNode->m_child, params, results);
	}

	return returnNodeIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeIK::InitIk(const DC::IkNodeConfig* pConfig,
								DC::AnimInfoCollection& info,
								const SnapshotAnimNodeTreeParams& params)
{
	m_configName = pConfig ? pConfig->m_name : INVALID_STRING_ID_64;

	typedef JacobianMap::EndEffectorDef EED;
	EED* paEndEffs = STACK_ALLOC(EED, pConfig->m_endEffectors->GetSize());
	typedef const DcArray<const DC::ScriptLambda*> ScriptLambaArray;

	const ScriptValue argv[] = { ScriptValue(&info) };

	int iEndEff = 0;
	for (const DC::ScriptLambda* pLambda : *pConfig->m_endEffectors)
	{
		VALIDATE_LAMBDA(pLambda, "ik-end-effector-func", m_animState.m_name.m_symbol);
		ScriptValue lambdaResult = ScriptManager::Eval(pLambda, SID("ik-end-effector-func"), ARRAY_COUNT(argv), argv);
		const DC::IkEndEffectorDef* pEndEffDef = reinterpret_cast<const DC::IkEndEffectorDef*>(lambdaResult.m_pointer);

		EndEffectorFromDC(pEndEffDef, paEndEffs[iEndEff++]);
	}

	CustomAllocateJanitor jj(m_memory, sizeof(m_memory), FILE_LINE_FUNC);

	m_pJocobianMap = NDI_NEW JacobianMap();
	m_pJocobianMap->Init(params.m_pAnimData->m_pPluginJointSet, pConfig->m_root, iEndEff, paEndEffs);
	m_pIkInstance = NDI_NEW JacobianIkInstance();
	m_pIkInstance->m_maxIterations = pConfig->m_maxIterations;
	if (pConfig->m_solverDamping >= 0.0f)
	{
		m_pIkInstance->m_solverDampingFactor = pConfig->m_solverDamping;
	}
	m_pIkInstance->m_disableJointLimits = !pConfig->m_useJointLimits;

	m_pGoalFuncs = pConfig->m_goals;
	m_pBlendFunc = pConfig->m_blend;

	m_goals.Init(iEndEff, FILE_LINE_FUNC);
	m_goals.Resize(iEndEff);

	m_flipped = params.m_stateFlipped;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ForceLinkAnimSnapshotNodeIk()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("alloc-ik-goal-look-at", DCAllocIkGoalLookAt)
{
	DC::IkGoalLookAt* pGoal = NDI_NEW(kAllocSingleFrame) DC::IkGoalLookAt;
	pGoal->m_typeId			= SID("ik-goal-look-at");
	pGoal->m_space = DC::kIkGoalSpaceWorld;
	pGoal->m_pos   = kOrigin;
	pGoal->m_axis  = DC::kIkGoalAxisZ;

	return ScriptValue(pGoal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("alloc-ik-goal-position", DCAllocIkGoalPosition)
{
	DC::IkGoalPosition* pGoal = NDI_NEW(kAllocSingleFrame) DC::IkGoalPosition;
	pGoal->m_typeId = SID("ik-goal-position");
	pGoal->m_space	= DC::kIkGoalSpaceWorld;
	pGoal->m_pos	= kOrigin;

	return ScriptValue(pGoal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("alloc-ik-goal-rotation", DCAllocIkGoalRotation)
{
	DC::IkGoalRotation* pGoal = NDI_NEW(kAllocSingleFrame) DC::IkGoalRotation;
	pGoal->m_typeId = SID("ik-goal-rotation");
	pGoal->m_space	= DC::kIkGoalSpaceWorld;
	pGoal->m_rot	= kIdentity;

	return ScriptValue(pGoal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("alloc-ik-goal-look-dir", DCAllocIkGoalLookDir)
{
	DC::IkGoalLookDir* pGoal = NDI_NEW(kAllocSingleFrame) DC::IkGoalLookDir;
	pGoal->m_typeId	   = SID("ik-goal-look-dir");
	pGoal->m_space	   = DC::kIkGoalSpaceWorld;
	pGoal->m_targetDir = kUnitZAxis;
	pGoal->m_lookAxis  = kUnitZAxis;

	return ScriptValue(pGoal);
}
