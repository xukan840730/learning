/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/anim-layer-ik.h"

#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-node-weapon-ik-feather.h"
#include "ndlib/anim/anim-snapshot-node-blend.h"
#include "ndlib/anim/nd-anim-util.h"

#include "gamelib/anim/gesture-node.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static float EvaluateWeaponIkFeatherFactorRecursive(const AnimStateSnapshot* pSnapshot, U32F nodeIndex)
{
	const AnimSnapshotNode* pNode = pSnapshot->GetSnapshotNode(nodeIndex);
	const StringId64 dcType = pNode->GetDcType();

	float res = 0.0f;

	switch (dcType.GetValue())
	{
	case SID_VAL("anim-node-gesture"):
		{
			const IGestureNode* pGestureNode = IGestureNode::FromAnimNode(pNode);
			const DC::GestureDef* pDcDef = pGestureNode ? pGestureNode->GetDcDef() : nullptr;
			if (pDcDef && pDcDef->m_weaponIkFeather)
			{
				res = 1.0f;
			}
		}
		break;

	case SID_VAL("anim-node-blend"):
		{
			const AnimSnapshotNodeBlend* pAnimNodeBlend = AnimSnapshotNodeBlend::FromAnimNode(pNode);

			if (pAnimNodeBlend->IsAdditiveBlend(pSnapshot))
			{
				const float leftVal	 = EvaluateWeaponIkFeatherFactorRecursive(pSnapshot, pAnimNodeBlend->m_leftIndex);
				const float rightVal = EvaluateWeaponIkFeatherFactorRecursive(pSnapshot, pAnimNodeBlend->m_rightIndex);

				res = Max(leftVal, rightVal);
			}
			else if (pAnimNodeBlend->m_blendFactor <= 0.0f)
			{
				res = EvaluateWeaponIkFeatherFactorRecursive(pSnapshot, pAnimNodeBlend->m_leftIndex);
			}
			else if (pAnimNodeBlend->m_blendFactor >= 1.0f)
			{
				res = EvaluateWeaponIkFeatherFactorRecursive(pSnapshot, pAnimNodeBlend->m_rightIndex);
			}
			else
			{
				const float leftVal	 = EvaluateWeaponIkFeatherFactorRecursive(pSnapshot, pAnimNodeBlend->m_leftIndex);
				const float rightVal = EvaluateWeaponIkFeatherFactorRecursive(pSnapshot, pAnimNodeBlend->m_rightIndex);

				res = Lerp(leftVal, rightVal, pAnimNodeBlend->m_blendFactor);
			}
		}
		break;

	default:
		if (pNode->IsKindOf(g_type_AnimSnapshotNodeUnary))
		{
			const AnimSnapshotNodeUnary* pUnaryNode = (const AnimSnapshotNodeUnary*)pNode;

			res = EvaluateWeaponIkFeatherFactorRecursive(pSnapshot, pUnaryNode->GetChildIndex());
		}
		break;
	}

	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct WeaponIkFeatherBlend
{
	float m_stateBlend	 = 0.0f;
	float m_gestureBlend = 0.0f;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static WeaponIkFeatherBlend GetWeaponIkFeatherFactorForInstance(const AnimStateInstance* pInstance)
{
	WeaponIkFeatherBlend res = WeaponIkFeatherBlend();
	
	const DC::AnimStateFlag stateFlags = pInstance ? pInstance->GetStateFlags() : 0ULL;

	if (stateFlags & DC::kAnimStateFlagWeaponIkFeatherVariable)
	{
		const AnimStateSnapshot& snapshot = pInstance->GetAnimStateSnapshot();
		res.m_gestureBlend = EvaluateWeaponIkFeatherFactorRecursive(&snapshot, snapshot.m_rootNodeIndex);
	}

	if (stateFlags & DC::kAnimStateFlagWeaponIkFeather)
	{
		res.m_stateBlend = 1.0f;
	}

	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class WeaponIkFeatherFactorBlender : public AnimStateLayer::InstanceBlender<WeaponIkFeatherBlend>
{
public:
	virtual WeaponIkFeatherBlend GetDefaultData() const override { return WeaponIkFeatherBlend(); }
	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, WeaponIkFeatherBlend* pDataOut) override
	{
		*pDataOut = GetWeaponIkFeatherFactorForInstance(pInstance);
		return true;
	}

	virtual WeaponIkFeatherBlend BlendData(const WeaponIkFeatherBlend& leftData,
										   const WeaponIkFeatherBlend& rightData,
										   float masterFade,
										   float animFade,
										   float motionFade) override
	{
		return { Lerp(leftData.m_stateBlend, rightData.m_stateBlend, animFade),
				 Lerp(leftData.m_gestureBlend, rightData.m_gestureBlend, animFade) };
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer_BlendedIkCb_PreBlend(const AnimStateLayer* pStateLayer,
										 const AnimCmdGenLayerContext& context,
										 AnimCmdList* pAnimCmdList,
										 SkeletonId skelId,
										 I32F leftInstace,
										 I32F rightInstance,
										 I32F outputInstance,
										 ndanim::BlendMode blendMode,
										 uintptr_t userData)
{
	if (context.m_pAnimData->m_animLod >= DC::kAnimLodFar)
	{
		return;
	}

	const WeaponIkFeatherBlend blendPair = WeaponIkFeatherFactorBlender().BlendForward(pStateLayer,
																					   WeaponIkFeatherBlend());

	const float weaponIkFeatherBlend = Max(blendPair.m_stateBlend, blendPair.m_gestureBlend);

	if (weaponIkFeatherBlend > 0.0f)
	{
		if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_debugWeaponIkFeather))
		{
			const Process* pOwner = context.m_pAnimData->m_hProcess.ToProcess();
			MsgConPauseable("[%s] weapon ik feather blend: %0.4f (state: %0.4f, gesture: %0.4f)\n",
							pOwner ? pOwner->GetName() : nullptr,
							weaponIkFeatherBlend,
							blendPair.m_stateBlend,
							blendPair.m_gestureBlend);
		}

		DC::WeaponIkFeatherParams params;
		params.m_twoBoneMode = true;
		params.m_domHand	 = DC::kHandIkHandLeft;
		params.m_masterFade	 = weaponIkFeatherBlend;
		params.m_ikFade		 = 1.0f;

		AnimSnapshotNodeWeaponIkFeather::GenerateCommands_PostBlend(pAnimCmdList, rightInstance, params);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer_BlendedIkCb_PostBlend(const AnimStateLayer* pStateLayer,
										  const AnimCmdGenLayerContext& context,
										  AnimCmdList* pAnimCmdList,
										  SkeletonId skelId,
										  I32F leftInstace,
										  I32F rightInstance,
										  I32F outputInstance,
										  ndanim::BlendMode blendMode,
										  uintptr_t userData)
{
}
