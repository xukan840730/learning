/*
 * Copyright (c) 2013 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/hand-fix-ik-plugin.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-node-hand-fix-ik.h"
#include "ndlib/anim/armik.h"
#include "ndlib/anim/ik/ik-chain-setup.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/ik/two-bone-ik.h"
#include "ndlib/nd-options.h"
#include "ndlib/profiling/profiling.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void HandFixIk_PreBlendCallback(const AnimStateLayer* pStateLayer,
								const AnimCmdGenLayerContext& context,
								AnimCmdList* pAnimCmdList,
								SkeletonId skelId,
								I32F leftInstance,
								I32F rightInstance,
								I32F outputInstance,
								ndanim::BlendMode blendMode,
								uintptr_t userData)
{
	AnimSnapshotNodeHandFixIk::GenerateHandFixIkCommands_PreBlend(pAnimCmdList,
																  leftInstance,
																  rightInstance,
																  outputInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void HandFixIk_PostBlendCallback(const AnimStateLayer* pStateLayer,
								 const AnimCmdGenLayerContext& context,
								 AnimCmdList* pAnimCmdList,
								 SkeletonId skelId,
								 I32F leftInstance,
								 I32F rightInstance,
								 I32F outputInstance,
								 ndanim::BlendMode blendMode,
								 uintptr_t userData)
{
	HandFixIkPluginCallbackArg* pArg = (HandFixIkPluginCallbackArg*)userData;

	AnimSnapshotNodeHandFixIk::GenerateHandFixIkCommands_PostBlend(pAnimCmdList,
																   pArg,
																   leftInstance,
																   rightInstance,
																   outputInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Locator FlipWristLocator(const Locator& loc)
{
	const Point inPos = loc.Pos();
	const Quat inRot  = loc.Rot();

	const Point outPos = Point(-inPos.X(), inPos.Y(), inPos.Z());
	const Quat outRot  = Quat(-inRot.X(), inRot.Y(), inRot.Z(), -inRot.W()) * QuatFromAxisAngle(kUnitXAxis, PI);

	return Locator(outPos, outRot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
OrbisAnim::Status HandFixIkPluginCallback(const HandFixIkPluginParams* pParams)
{
	if (!pParams)
	{
		return OrbisAnim::kFatalErrorUsage;
	}

	if (pParams->m_tt <= 0.0f)
	{
		return OrbisAnim::kSuccess;
	}

	ndanim::JointParams* pJointParamsLs = pParams->m_pJointParamsLs;
	const ndanim::JointParams* pJointParamsPreAdditiveLs = pParams->m_pJointParamsPreAdditiveLs;

	const bool flipped = pParams->m_flipped;

	const I32 numJoints = OrbisAnim::kJointGroupSize;

	JointSet* pJointSet = pParams->m_pJointSet;

	if (!pJointSet)
	{
		return OrbisAnim::kFatalErrorUsage;
	}

	if (!pJointSet->ReadFromJointParams(pJointParamsPreAdditiveLs, 0, numJoints, 1.0f))
	{
		return OrbisAnim::kSuccess;
	}

	const ArmIkChainSetup* pLeftArmChain = pParams->m_apArmChains[kLeftArm];
	const ArmIkChainSetup* pRightArmChain = pParams->m_apArmChains[kRightArm];

	if (!pLeftArmChain || !pRightArmChain)
	{
		return OrbisAnim::kFatalErrorUsage;
	}

	const I32F jointOffsets[kArmCount][3] = {
		{ pJointSet->GetJointOffset(pLeftArmChain->GetShoulderJointIndex()),
		  pJointSet->GetJointOffset(pLeftArmChain->GetElbowJointIndex()),
		  pJointSet->GetJointOffset(pLeftArmChain->GetWristJointIndex()) },
		{ pJointSet->GetJointOffset(pRightArmChain->GetShoulderJointIndex()),
		  pJointSet->GetJointOffset(pRightArmChain->GetElbowJointIndex()),
		  pJointSet->GetJointOffset(pRightArmChain->GetWristJointIndex()) },
	};

	const Locator wristLocsWs[] = { pJointSet->GetJointLocWs(jointOffsets[kLeftArm][2]),
									pJointSet->GetJointLocWs(jointOffsets[kRightArm][2]) };

	const Locator goalLocsWs[] = {
		flipped ? FlipWristLocator(wristLocsWs[kRightArm]) : wristLocsWs[kLeftArm],
		flipped ? FlipWristLocator(wristLocsWs[kLeftArm]) : wristLocsWs[kRightArm],
	};

	pJointSet->DiscardJointCache();
	
	if (pJointSet->ReadFromJointParams(pParams->m_pJointParamsLs, 0, numJoints, 1.0f))
	{
		for (U32F iArm = 0; iArm < 2; ++iArm)
		{
			if (!pParams->m_handsToIk[iArm])
				continue;

			TwoBoneIkParams params;
			params.m_tt = pParams->m_tt;
			params.m_pJointSet		 = pJointSet;
			params.m_goalPos		 = goalLocsWs[iArm].Pos();
			params.m_finalGoalRot	 = goalLocsWs[iArm].Rot();
			params.m_jointOffsets[0] = jointOffsets[iArm][0];
			params.m_jointOffsets[1] = jointOffsets[iArm][1];
			params.m_jointOffsets[2] = jointOffsets[iArm][2];

			params.m_abortIfCantSolve = false;
			params.m_objectSpace	  = false;

			TwoBoneIkResults results;
			SolveTwoBoneIK(params, results);
		}

		pJointSet->WriteJointParamsBlend(1.0f, pJointParamsLs, 0, numJoints);

		if (pParams->m_pValidBitsOut)
		{
			for (U32F iArm = 0; iArm < 2; ++iArm)
			{
				if (!pParams->m_handsToIk[iArm])
					continue;

				pJointSet->WriteJointValidBits(jointOffsets[iArm][2], 0, pParams->m_pValidBitsOut);
			}
		}
	}

	return OrbisAnim::kSuccess;
}
