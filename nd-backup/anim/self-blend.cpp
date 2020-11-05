/*
 * Copyright (c) 2018 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/self-blend.h"

#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/nd-anim-align-util.h"

/// --------------------------------------------------------------------------------------------------------------- ///
TYPE_FACTORY_REGISTER(SelfBlendAction, NdSubsystemAnimAction);

/// --------------------------------------------------------------------------------------------------------------- ///
Err SelfBlendAction::Init(const SubsystemSpawnInfo& info)
{
	Err res = ParentClass::Init(info);
	if (res.Failed())
	{
		return res;
	}

	if (!info.m_pUserData)
	{
		return Err::kErrBadData;
	}

	if (info.m_pUserData)
	{
		m_params = *(const Params*)info.m_pUserData;
	}

	SetActionState(NdSubsystemAnimAction::ActionState::kUnattached);

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SelfBlendAction::InstancePrepare(const DC::AnimState* pAnimState, FadeToStateParams* pParams)
{
	if (!pParams)
		return;

	if (pParams->m_apRefValid)
	{
		m_sourceAp = pParams->m_apRef;

		if (pParams->m_customApRefId != INVALID_STRING_ID_64)
		{
			m_srcApChannelId = pParams->m_customApRefId;
		}
		else
		{
			m_srcApChannelId = SID("apReference");
		}
	}
	else
	{
		m_sourceAp		 = m_params.m_destAp;
		m_srcApChannelId = m_params.m_apChannelId;
	}

	pParams->m_apRef		 = m_params.m_destAp;
	pParams->m_customApRefId = m_params.m_apChannelId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SelfBlendAction::InstanceAlignFunc(const AnimStateInstance* pInst,
										const BoundFrame& prevAlign,
										const BoundFrame& currAlign,
										const Locator& apAlignDelta,
										BoundFrame* pAlignOut,
										bool debugDraw)
{
	if (!pInst || !pAlignOut || (m_params.m_blendParams.m_phase < 0.0f))
		return false;

	const float selfBlendTT = GetSelfBlendParam(pInst);
	const float selfBlend	= (selfBlendTT >= 0.0f) ? CalculateCurveValue(selfBlendTT, m_params.m_blendParams.m_curve)
												  : -1.0f;

	BoundFrame sourceAp = m_sourceAp;
	BoundFrame destAp = m_params.m_destAp;

	const Vector apRAdjPs = pInst->GetApRestrictAdjustmentPs();
	sourceAp.AdjustTranslationPs(apRAdjPs);
	destAp.AdjustTranslationPs(apRAdjPs);

	BoundFrame ret = currAlign;

	if (selfBlend <= 0.0f)
	{
		const Locator srcApAlignDelta = NdAnimAlign::GetApToAlignDelta(pInst, m_srcApChannelId);
		const Locator alignWs = sourceAp.GetLocator().TransformLocator(srcApAlignDelta);

		ret.SetLocator(alignWs);
	}
	else if (selfBlend >= 1.0f)
	{
		const Locator dstApAlignDelta = NdAnimAlign::GetApToAlignDelta(pInst, m_params.m_apChannelId);
		const Locator alignWs = destAp.GetLocator().TransformLocator(dstApAlignDelta);

		ret.SetLocator(alignWs);
	}
	else
	{
		const Locator srcApAlignDelta = NdAnimAlign::GetApToAlignDelta(pInst, m_srcApChannelId);
		const Locator dstApAlignDelta = NdAnimAlign::GetApToAlignDelta(pInst, m_params.m_apChannelId);

		const Locator startAlignWs = sourceAp.GetLocator().TransformLocator(srcApAlignDelta);
		const Locator destAlignWs = destAp.GetLocator().TransformLocator(dstApAlignDelta);

		const Locator alignWs = Lerp(startAlignWs, destAlignWs, selfBlend);

		ret.SetLocator(alignWs);

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			g_prim.Draw(DebugCoordAxesLabeled(startAlignWs, "[sb] startAlign", 0.3f, kPrimEnableHiddenLineAlpha, 2.0f, kColorWhite, 0.6f));
			g_prim.Draw(DebugCoordAxesLabeled(alignWs, "[sb] currAlign", 0.3f, kPrimEnableHiddenLineAlpha, 2.0f, kColorWhite, 0.6f));
			g_prim.Draw(DebugCoordAxesLabeled(destAlignWs, "[sb] destAlign", 0.3f, kPrimEnableHiddenLineAlpha, 2.0f, kColorWhite, 0.6f));
			// g_prim.Draw(DebugCoordAxesLabeled(destAp.GetLocatorWs(), "[sb] destAp", 0.3f, kPrimEnableHiddenLineAlpha, 2.0f, kColorWhite, 0.6f));
			// g_prim.Draw(DebugCoordAxesLabeled(sourceAp.GetLocatorWs(), "[sb] sourceAp", 0.3f, kPrimEnableHiddenLineAlpha, 2.0f, kColorWhite, 0.6f));
		}
	}

	*pAlignOut = ret;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float SelfBlendAction::GetCompletionPhase() const
{
	const AnimStateInstance* pInst = GetInstanceStart();
	return GetSelfBlendParam(pInst);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float SelfBlendAction::GetSelfBlendParam(const AnimStateInstance* pInst) const
{
	if (!pInst || (m_params.m_blendParams.m_phase < 0.0f))
	{
		return -1.0f;
	}

	const float duration = pInst->GetDuration();
	const float blendTime = m_params.m_blendParams.m_time;
	const float startPhase = m_params.m_blendParams.m_phase;
	float endPhase = startPhase;

	if (duration > 0.0f)
	{
		endPhase = (blendTime / duration) + startPhase;
	}

	if (m_params.m_constraintPhase >= 0.0f)
	{
		endPhase = Min(endPhase, m_params.m_constraintPhase);
	}

	const float curPhase = pInst->Phase();
	const float selfBlendTT = LerpScaleClamp(startPhase, endPhase, 0.0f, 1.0f, curPhase);

	return selfBlendTT;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SelfBlendAction* StartSelfBlend(NdGameObject* pOwner,
								const SelfBlendAction::Params& sbParams,
								StateChangeRequest::ID requestId,
								StringId64 layerId /* = INVALID_STRING_ID_64 */)
{
	SubsystemSpawnInfo sbSpawnInfo(SID("SelfBlendAction"), pOwner);
	sbSpawnInfo.m_pUserData = &sbParams;
	SelfBlendAction* pSelfBlend = (SelfBlendAction*)NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap,
																		sbSpawnInfo,
																		FILE_LINE_FUNC);

	if (pSelfBlend)
	{
		pSelfBlend->BindToAnimRequest(requestId, layerId);
	}

	return pSelfBlend;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SelfBlendAction* StartSelfBlend(NdGameObject* pOwner, const SelfBlendAction::Params& sbParams, AnimStateInstance* pInst)
{
	SubsystemSpawnInfo sbSpawnInfo(SID("SelfBlendAction"), pOwner);
	sbSpawnInfo.m_pUserData = &sbParams;
	SelfBlendAction* pSelfBlend = (SelfBlendAction*)NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap,
																		sbSpawnInfo,
																		FILE_LINE_FUNC);

	if (pSelfBlend)
	{
		pSelfBlend->BindToInstance(pInst);

		pSelfBlend->UpdateSourceApRef(pInst->GetApLocator(), pInst->GetCustomApRefId());

		pInst->SetApLocator(sbParams.m_destAp);
		pInst->SetCustomApRefId(sbParams.m_apChannelId);
	}

	return pSelfBlend;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SelfBlendAction::GetAnimControlDebugText(const AnimStateInstance* pInstance, IStringBuilder* pText) const
{
	STRIP_IN_FINAL_BUILD_VALUE(false);

	if (const AnimStateInstance* pInst = GetInstanceStart())
	{
		const float param = GetSelfBlendParam(pInst);
		pText->append_format("SelfBlendAction %0.3f [%0.2fs %s @ %0.1f]",
							 param,
							 m_params.m_blendParams.m_time,
							 DC::GetAnimCurveTypeName(m_params.m_blendParams.m_curve),
							 m_params.m_blendParams.m_phase);
	}
	else
	{
		pText->append("SelfBlend <no instance!>");
	}

	return true;
}
