/*
 * Copyright (c) 2018 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/gameplay/nd-subsystem-anim-action.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class SelfBlendAction : public NdSubsystemAnimAction
{
public:
	typedef NdSubsystemAnimAction ParentClass;

	struct Params
	{
		BoundFrame m_destAp = BoundFrame(kIdentity);
		StringId64 m_apChannelId = SID("apReference");
		float m_constraintPhase = -1.0f;
		DC::SelfBlendParams m_blendParams;
	};

	virtual Err Init(const SubsystemSpawnInfo& info) override;

	virtual void InstancePrepare(const DC::AnimState* pAnimState, FadeToStateParams* pParams) override;

	virtual bool InstanceAlignFunc(const AnimStateInstance* pInst,
								   const BoundFrame& prevAlign,
								   const BoundFrame& currAlign,
								   const Locator& apAlignDelta,
								   BoundFrame* pAlignOut,
								   bool debugDraw) override;

	const Params& GetParams() const { return m_params; }
	float GetCompletionPhase() const;

	const StringId64 GetSourceChannelId() const { return m_srcApChannelId; }
	const BoundFrame& GetSourceApRef() const { return m_sourceAp; }
	const BoundFrame& GetDestApRef() const { return m_params.m_destAp; }

	void UpdateParams(const Params& newParams) { m_params = newParams; }
	void UpdateDestApRef(const BoundFrame& newAp) { m_params.m_destAp = newAp; }
	void UpdateSourceApRef(const BoundFrame& srcAp) { m_sourceAp = srcAp; }
	void UpdateSourceApRef(const BoundFrame& srcAp, StringId64 apChannelId)
	{
		m_sourceAp = srcAp;

		if (apChannelId != INVALID_STRING_ID_64)
		{
			m_srcApChannelId = apChannelId;
		}
		else
		{
			m_srcApChannelId = SID("apReference");
		}
	}

	virtual bool GetAnimControlDebugText(const AnimStateInstance* pInstance, IStringBuilder* pText) const override;

private:
	float GetSelfBlendParam(const AnimStateInstance* pInst) const;

	BoundFrame m_sourceAp = BoundFrame(kIdentity);
	StringId64 m_srcApChannelId = SID("apReference");

	Params m_params;
};

typedef TypedSubsystemHandle<SelfBlendAction> SelfBlendHandle;

TYPE_FACTORY_DECLARE(SelfBlendAction);

/// --------------------------------------------------------------------------------------------------------------- ///
SelfBlendAction* StartSelfBlend(NdGameObject* pOwner,
								const SelfBlendAction::Params& sbParams,
								StateChangeRequest::ID requestId,
								StringId64 layerId = INVALID_STRING_ID_64);

SelfBlendAction* StartSelfBlend(NdGameObject* pOwner, const SelfBlendAction::Params& sbParams, AnimStateInstance* pInst);
