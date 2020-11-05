/*
 * Copyright (c) 2009 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "game/ai/controller/face-controller.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/profiling/profiling.h"

#include "gamelib/audio/lipsync.h"
#include "gamelib/scriptx/h/npc-demeanor-defines.h"
#include "gamelib/cinematic/cinematic-manager.h"
#include "game/ai/agent/npc.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/scriptx/h/anim-npc-info.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AiFaceController : public IAiFaceController
{
public:
	bool m_fadedIn = false;

	virtual void MeleeOverrideFacialAnimation(StringId64 animId, F32 duration) override
	{
		m_meleeOverrideAnim = animId;
		m_requestedThisFrame = true;
		m_overrideEndTime = GetCharacter()->GetClock()->GetCurTime() + Seconds(duration);
	}

	virtual void ClearMeleeOverride() override
	{
		m_meleeOverrideAnim = INVALID_STRING_ID_64;
		m_requestedThisFrame = false;
	}

private:
	virtual void UpdateStatus() override;
	virtual bool IsBusy() const override;

	StringId64 GetDesiredFacialAnimId() const;

	CachedAnimLookup m_animLookup;
	StringId64 m_meleeOverrideAnim = INVALID_STRING_ID_64;
	bool m_requestedThisFrame	   = false;
	TimeFrame m_overrideEndTime	   = TimeFrameNegInfinity();
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiFaceController::UpdateStatus()
{
	PROFILE(AI, FaceCon_UpdateStatus);

	NdGameObject* pGo = GetCharacterGameObject();
	NavCharacter* pNavChar = GetCharacter();
	SimpleNavCharacter* pSimpleNavChar = GetSimpleNavCharacter();
	AnimControl* pAnimControl = pGo ? pGo->GetAnimControl() : nullptr;
	AnimLayer* pFacialLayer = pAnimControl->GetLayerById(SID("facial-base"));

	if (!pFacialLayer)
	{
		return;
	}

	if (pNavChar && pNavChar->IsRagdollPowerFadedOut())
	{
		return;
	}

	const SsAnimateController* pSsAnimController = pGo ? pGo->GetPrimarySsAnimateController() : nullptr;
	if (pSsAnimController && pSsAnimController->IsPlayingCinematic())
	{
		if (pFacialLayer->GetDesiredFade() > 0.0f)
		{
			const AnimStateLayer* pIgcLayer = pSsAnimController->GetStateLayer();
			const AnimStateInstance* pIgcInst = pIgcLayer ? pIgcLayer->CurrentStateInstance() : nullptr;
			if (pIgcInst && (pIgcInst->AnimFade() >= 1.0f) && !pIgcLayer->AreTransitionsPending())
			{
				pFacialLayer->Fade(0.0f, 0.5f);
			}
		}
		return;
	}
	else
	{
		pFacialLayer->Fade(1.0f, 0.5f);
	}

	const AnimLayerType type = pFacialLayer->GetType();
	AnimSimpleLayer* pFacialSimpleLayer = (type == kAnimLayerTypeSimple) ? (AnimSimpleLayer*)pFacialLayer : nullptr;
	AnimStateLayer* pFacialStateLayer = (type == kAnimLayerTypeState) ? (AnimStateLayer*)pFacialLayer : nullptr;

	if (m_meleeOverrideAnim)
	{
		if (!m_requestedThisFrame && GetCharacter()->GetClock()->GetCurTime() > m_overrideEndTime)
		{
			m_meleeOverrideAnim = INVALID_STRING_ID_64;
		}

		m_requestedThisFrame = false;
	}

	StringId64 curAnim = INVALID_STRING_ID_64;
	StringId64 desAnim = GetDesiredFacialAnimId();
	StringId64 curStateId = INVALID_STRING_ID_64;

	if (pFacialSimpleLayer)
	{
		const AnimSimpleInstance* pInst = pFacialSimpleLayer->CurrentInstance();
		curAnim = pInst ? pInst->GetAnimId() : INVALID_STRING_ID_64;
		curStateId = SID("simple-layer");
	}
	else if (pFacialStateLayer)
	{
		const AnimStateInstance* pInst = pFacialStateLayer->CurrentStateInstance();
		curAnim = pInst ? pInst->GetPhaseAnim() : INVALID_STRING_ID_64;
		curStateId = pInst ? pInst->GetStateName() : INVALID_STRING_ID_64;
	}

	StringId64 desEmotionAnimId = INVALID_STRING_ID_64;
	float emotionFade = 1.0f;

	bool useFaceEmotionState = false;
	const EmotionControl* pEmoControl = pGo->GetEmotionControl();

	bool isDead = false;
	if (pNavChar)
		isDead = pNavChar->IsDead();

	float fadetime = 3.0f;
	if (pEmoControl && (pEmoControl->GetEmotionalState().m_emotion != INVALID_STRING_ID_64) && !m_meleeOverrideAnim)
	{
		if (!isDead || pEmoControl->GetEmotionalState().m_priority >= kEmotionPriorityDeath)
		{
			desAnim = GetIdleEmotionAnim(pGo, pEmoControl->GetEmotionalState().m_emotion);
			desEmotionAnimId = GetBaseEmotionAnim(pGo, pEmoControl->GetEmotionalState().m_emotion);
			emotionFade = pEmoControl->GetEmotionalState().m_fade;
			useFaceEmotionState = true;
			fadetime = pEmoControl->GetEmotionalState().m_blend;
		}
	}

	if (m_animLookup.GetSourceId() != desAnim)
	{
		m_animLookup.SetSourceId(desAnim);
	}

	m_animLookup = pAnimControl->LookupAnimCached(m_animLookup);
	
	const ArtItemAnimHandle hDesAnim = m_animLookup.GetAnim();
	const ArtItemAnim* pDesAnim = hDesAnim.ToArtItem();
	const StringId64 desAnimId = pDesAnim ? pDesAnim->GetNameId() : INVALID_STRING_ID_64;

	if (FALSE_IN_FINAL_BUILD(desAnim && !pDesAnim && g_gameObjectDrawFilter.m_drawAnimControl
							 && DebugSelection::Get().IsProcessSelected(pGo)))
	{
		MsgCon("[%s] Facial Base Anim %s%s%s [%s] not found!\n",
			   pGo->GetName(),
			   GetTextColorString(kTextColorRed),
			   DevKitOnly_StringIdToString(desAnim),
			   GetTextColorString(kTextColorNormal),
			   DevKitOnly_StringIdToString(m_animLookup.GetFinalResolvedId()));
	}

	if (!m_fadedIn)
		fadetime = 0.0f;

	DC::AnimNpcInstanceInfo* pInfo = pAnimControl->InstanceInfo<DC::AnimNpcInstanceInfo>();
	pInfo->m_facialEmotionFade = emotionFade;

	if (pFacialStateLayer)
	{
		if (AnimStateInstance* pStateInstance = pFacialStateLayer->CurrentStateInstance())
		{
			if (DC::AnimNpcInstanceInfo* pNpcInstanceInfo = (DC::AnimNpcInstanceInfo*)pStateInstance->GetAnimInstanceInfo())
			{
				if (pNpcInstanceInfo->m_facialEmotionAnim == desEmotionAnimId)
					pNpcInstanceInfo->m_facialEmotionFade = emotionFade;
			}
		}
	}

	bool hasFreeInstance = false;
	if (pFacialStateLayer && pFacialStateLayer->HasFreeInstance())
		hasFreeInstance = true;
	if (pFacialSimpleLayer && pFacialSimpleLayer->HasFreeInstance())
		hasFreeInstance = true;

	if (hasFreeInstance
		&& ((curAnim != desAnimId || (curStateId != SID("s_facial-emotion") && useFaceEmotionState))
			|| (desEmotionAnimId != pInfo->m_facialEmotionAnim && useFaceEmotionState)))
	{
		pInfo->m_facialEmotionIdleAnim = m_animLookup.GetSourceId();
		pInfo->m_facialEmotionAnim = desEmotionAnimId;

		m_fadedIn = true;

		if (desAnimId == INVALID_STRING_ID_64)
		{
			pFacialLayer->Fade(0.0f, fadetime, DC::kAnimCurveTypeEaseIn);
		}
		else
		{
			switch (type)
			{
			case kAnimLayerTypeSimple:
				{
					AnimSimpleLayer::FadeRequestParams params;
					params.m_forceLoop = !isDead;
					params.m_fadeTime = fadetime;
					params.m_blendType = DC::kAnimCurveTypeEaseIn;

					if (pFacialSimpleLayer->RequestFadeToAnim(hDesAnim, params))
					{
						pFacialSimpleLayer->Fade(1.0f, fadetime, DC::kAnimCurveTypeEaseIn);
					}
					else
					{
						pFacialSimpleLayer->Fade(0.0f, fadetime, DC::kAnimCurveTypeEaseIn);
					}
				}
				break;

			case kAnimLayerTypeState:
				{
					FadeToStateParams params;
					params.m_animFadeTime = fadetime;
					pFacialStateLayer->Fade(1.0f, fadetime, DC::kAnimCurveTypeEaseIn);
					if (useFaceEmotionState)
					{
						pFacialStateLayer->FadeToState(SID("s_facial-emotion"), params);
					}
					else
					{
						pInfo->m_facialBaseAnim = desAnim;
						pFacialStateLayer->FadeToState(SID("s_facial-base"), params);
					}
				}
				break;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiFaceController::IsBusy() const
{
	// facial animations should never tie up the character
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiFaceController::GetDesiredFacialAnimId() const
{
	const Npc* pNpc = static_cast<const Npc*>(GetCharacter());

	const bool shooting = pNpc && (!pNpc->TimePassed(Seconds(3.0f), pNpc->GetLastShotTime()));

	const AnimationControllers* pAnimControllers = pNpc ? pNpc->GetAnimationControllers() : nullptr;
	const IAiMeleeActionController* pMeleeActionController = pAnimControllers ? pAnimControllers->GetMeleeActionController() : nullptr;
	const IAiHitController* pHitController = pAnimControllers ? pAnimControllers->GetHitController() : nullptr;

	const bool needMeleeFace = pMeleeActionController ? pMeleeActionController->IsBusyInMelee() : false;
	const bool isInHitReaction = pHitController ? pHitController->IsBusy() : false;

	StringId64 faceAnim = SID("face-idle");

	if (m_meleeOverrideAnim)
	{
		faceAnim = m_meleeOverrideAnim;
	}
	else if (pNpc && pNpc->IsDead())
	{
		faceAnim = SID("face-death");
	}
	else if (needMeleeFace)
	{
		faceAnim = SID("face-melee");
	}
	else if (isInHitReaction)
	{
		faceAnim = SID("face-reaction");
	}
	else if (pNpc)
	{
		Demeanor dem;

		if (shooting)
		{
			dem = pNpc->GetFacialDemeanorWhenShooting();
		}
		else
		{
			dem = pNpc->GetCurrentDemeanor();
		}

		const DC::NpcDemeanorDef* const* ppDemeanorDefinitions = pNpc->GetDemeanorDefinitions();
		const I32F numDemeanors = pNpc->GetNumDemeanors();
		const I32F demIndex = dem.ToI32();

		if ((demIndex >= 0) && (demIndex < numDemeanors))
		{
			const DC::NpcDemeanorDef* pDemeanorDef = ppDemeanorDefinitions[demIndex];

			if (pDemeanorDef && pDemeanorDef->m_faceSuffix && (strlen(pDemeanorDef->m_faceSuffix) > 0))
			{
				faceAnim = StringId64Concat(SID("face-idle-"), pDemeanorDef->m_faceSuffix);
			}
		}
	}

	return faceAnim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
IAiFaceController* CreateAiFaceController()
{
	return NDI_NEW AiFaceController;
}
