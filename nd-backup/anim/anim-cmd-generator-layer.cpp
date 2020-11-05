/*
 * Copyright (c) 2008 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-cmd-generator-layer.h"

#include "corelib/math/locator.h"
#include "corelib/memory/relocate.h"
#include "corelib/util/msg.h"

#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/feather-blend-table.h"
#include "ndlib/profiling/profiling.h"

/// --------------------------------------------------------------------------------------------------------------- ///
AnimCmdGeneratorLayer::AnimCmdGeneratorLayer(AnimTable* pAnimTable, AnimOverlaySnapshot* pOverlaySnapshot)
	: AnimLayer(kAnimLayerTypeCmdGenerator, pAnimTable, pOverlaySnapshot), m_pAnimCmdGenerator(nullptr)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimCmdGeneratorLayer::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	AnimLayer::Relocate(deltaPos, lowerBound, upperBound);

	RelocatePointer(m_pAnimCmdGenerator, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimCmdGeneratorLayer::IsValid() const
{
	return GetCurrentFade() > 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimCmdGeneratorLayer::GetCurrentFade() const
{
	return AnimLayer::GetCurrentFade() * m_pAnimCmdGenerator->GetFadeMult();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimCmdGeneratorLayer::CreateAnimCmds(const AnimCmdGenLayerContext& context, AnimCmdList* pAnimCmdList) const
{
	const U32F destInstance = context.m_instanceZeroIsValid ? 1 : 0;
	
	m_pAnimCmdGenerator->CreateAnimCmds(context, pAnimCmdList, destInstance);

	if (m_pAnimCmdGenerator->GeneratesPose())
	{
		const I32F iFeatherBlendEntry = m_pAnimCmdGenerator->GetFeatherBlendTableEntry();

		const float curFade = GetCurrentFade();

		const OrbisAnim::ChannelFactor* const* ppChannelFactors = nullptr;
		const U32* pNumChannelFactors = nullptr;

		const float blendVal = g_featherBlendTable.CreateChannelFactorsForAnimCmd(context.m_pAnimateSkel,
																				  iFeatherBlendEntry,
																				  curFade,
																				  &ppChannelFactors,
																				  &pNumChannelFactors);

		if (context.m_instanceZeroIsValid)
		{
			// blend all instances beyond the first down to the first
			if (ppChannelFactors)
			{
				pAnimCmdList->AddCmd_EvaluateFeatherBlend(0,
														  1,
														  0,
														  m_blendMode,
														  blendVal,
														  ppChannelFactors,
														  pNumChannelFactors,
														  iFeatherBlendEntry);
			}
			else
			{
				pAnimCmdList->AddCmd_EvaluateBlend(0, 1, 0, m_blendMode, blendVal);
			}
		}
		else if (m_blendMode == ndanim::kBlendAdditive)
		{
			// if we are here then our destInstance should be 0, so just use 1 for our stand-in LHS bindpose inst
			pAnimCmdList->AddCmd_EvaluateEmptyPose(1);

			if (ppChannelFactors)
			{
				if (ppChannelFactors)
				{
					pAnimCmdList->AddCmd_EvaluateFeatherBlend(1,
															  0,
															  0,
															  m_blendMode,
															  blendVal,
															  ppChannelFactors,
															  pNumChannelFactors,
															  iFeatherBlendEntry);
				}
				else
				{
					pAnimCmdList->AddCmd_EvaluateBlend(1, 0, 0, m_blendMode, blendVal);
				}
			}
			else
			{
				pAnimCmdList->AddCmd_EvaluateBlend(1, 0, 0, m_blendMode, blendVal);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator AnimCmdGeneratorLayer::EvaluateAP(StringId64 apChannelName) const
{
	return m_pAnimCmdGenerator->EvaluateChannel(apChannelName);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimCmdGeneratorLayer::Freeze() 
{ 
	m_pAnimCmdGenerator->Freeze(); 
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimCmdGeneratorLayer::Setup(StringId64 name, ndanim::BlendMode blendMode)
{
	ANIM_ASSERTF(false, ("This variant of AnimLayer::Setup() is not intended to be used on AnimCmdGeneratorLayer"));
	Setup(name, blendMode, nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimCmdGeneratorLayer::Setup(StringId64 name, ndanim::BlendMode blendMode, IAnimCmdGenerator* pAnimCmdGenerator)
{
	AnimLayer::Setup(name, blendMode);
	m_pAnimCmdGenerator = pAnimCmdGenerator;
	ANIM_ASSERT(m_pAnimCmdGenerator);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimCmdGeneratorLayer::BeginStep(F32 deltaTime, EffectList* pTriggeredEffects, const FgAnimData* pAnimData)
{
	PROFILE(Animation, AnimCmdGeneratorLayer_BeginStep);

	m_pAnimCmdGenerator->Step(deltaTime);
	if (GetCurrentFade() < 1.0f)
	{
		m_pAnimCmdGenerator->FillEffectList(nullptr);
	}
	else
	{
		m_pAnimCmdGenerator->FillEffectList(pTriggeredEffects);
	}

	AnimLayer::BeginStep(deltaTime, pTriggeredEffects, pAnimData);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimCmdGeneratorLayer::FinishStep(F32 deltaTime, EffectList* pTriggeredEffects)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimCmdGeneratorLayer::DebugPrint(MsgOutput output, U32 priority) const
{
	STRIP_IN_FINAL_BUILD;

	if (!Memory::IsDebugMemoryAvailable())
		return;

	if (!g_animOptions.m_debugPrint.ShouldShow(GetName()))
		return;

	if (AnimLayer::GetCurrentFade() <= 0.0f)
		return;

	if (!g_animOptions.m_debugPrint.m_simplified)
	{
		SetColor(output, 0xFF000000 | 0x00FFFFFF);
		PrintTo(output, "-----------------------------------------------------------------------------------------\n");

		SetColor(output, 0xFF000000 | 0x0055FF55);
		PrintTo(output,
				"AnimCmdGeneratorLayer \"%s\": [%s], Pri %d, Cur Fade: %1.2f, Des Fade: %1.2f\n",
				DevKitOnly_StringIdToString(m_name),
				m_blendMode == ndanim::kBlendSlerp ? "blend" : "additive",
				priority,
				GetCurrentFade(),
				GetDesiredFade());
	}

	m_pAnimCmdGenerator->DebugPrint(output);

	SetColor(output, 0xFF000000 | 0x00FFFFFF);
}
