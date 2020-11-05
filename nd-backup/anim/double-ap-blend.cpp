/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/double-ap-blend.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-actor.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/nd-anim-util.h"

#include "gamelib/gameplay/nd-game-object.h"

/// --------------------------------------------------------------------------------------------------------------- ///
AnimDoubleApBlendRequest::AnimDoubleApBlendRequest(StringId64 animStateId, 
												   const BoundFrame& apRef1,
												   const BoundFrame& apRef2,
												   float fadeTime, 
												   float motionFadeTime,
												   float doubleBlendTime, 
												   float startPhase /* = 0.0f */,
												   DC::AnimCurveType srcBlendType /* = DC::kAnimCurveTypeUniformS */,
												   DC::AnimCurveType dstBlendType /* = DC::kAnimCurveTypeLinear */)
{
	m_animStateId = animStateId;
	m_apRef[0] = apRef1;
	m_apRef[1] = apRef2;
	m_firstApRefValid = true;
	m_fadeTime = fadeTime;	
	m_motionFadeTime = motionFadeTime;
	m_doubleBlendTime = doubleBlendTime;
	m_startPhase = startPhase;		
	m_matchMode = kNone;
	m_animId = INVALID_STRING_ID_64;
	m_freezeSrcState = false;
	m_freezeDstState = false;
	m_preventFirstFrameUpdate = false;
	m_srcBlendType = srcBlendType;
	m_dstBlendType = dstBlendType;
	m_apRef1Id = INVALID_STRING_ID_64;
	m_customAlign = Locator();
	m_secondStartPhase = 0.0f;
	m_secondBlendDone = false;
	m_mirrored = false;
	m_newInstanceBehavior = FadeToStateParams::kUnspecified;
	m_customApRefId[0] = INVALID_STRING_ID_64;
	m_customApRefId[1] = INVALID_STRING_ID_64;

}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimDoubleApBlendRequest::AnimDoubleApBlendRequest(StringId64 animStateId, 
												   EAlignMatchMode matchMode, 
												   const BoundFrame& apRef2, 
												   float fadeTime, 
												   float motionFadeTime, 
												   float doubleBlendTime, 
												   float startPhase, 
												   DC::AnimCurveType srcBlendType, 
												   DC::AnimCurveType dstBlendType)
{
	m_animStateId = animStateId;
	m_apRef[1] = apRef2;	
	m_firstApRefValid = false;
	m_fadeTime = fadeTime;	
	m_motionFadeTime = motionFadeTime;
	m_doubleBlendTime = doubleBlendTime;
	m_startPhase = startPhase;		
	m_matchMode = matchMode;
	m_animId = INVALID_STRING_ID_64;
	m_freezeSrcState = false;
	m_freezeDstState = false;
	m_preventFirstFrameUpdate = false;
	m_srcBlendType = srcBlendType;
	m_dstBlendType = dstBlendType;
	m_apRef1Id = INVALID_STRING_ID_64;
	m_customAlign = Locator();
	m_secondStartPhase = 0.0f;
	m_secondBlendDone = false;
	m_mirrored = false;
	m_newInstanceBehavior = FadeToStateParams::kUnspecified;
	m_customApRefId[0] = INVALID_STRING_ID_64;
	m_customApRefId[1] = INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimDoubleApBlendRequest::AnimDoubleApBlendRequest(StringId64 animStateId,
												   StringId64 apRef1Id,
												   const Locator& customAlign, 
												   const BoundFrame& apRef2,
												   float fadeTime, 
												   float motionFadeTime, 
												   float doubleBlendTime,
												   float startPhase,
												   float secondStartPhase,
												   DC::AnimCurveType srcBlendType, 
												   DC::AnimCurveType dstBlendType)
{
	m_animStateId = animStateId;
	m_apRef[1] = apRef2;
	m_firstApRefValid = true;
	m_fadeTime = fadeTime;
	m_motionFadeTime = motionFadeTime;
	m_doubleBlendTime = doubleBlendTime;
	m_startPhase = startPhase;
	m_matchMode = kNone;
	m_animId = INVALID_STRING_ID_64;
	m_freezeSrcState = false;
	m_freezeDstState = false;
	m_preventFirstFrameUpdate = false;
	m_srcBlendType = srcBlendType;
	m_dstBlendType = dstBlendType;
	m_apRef1Id = apRef1Id;
	m_customAlign = customAlign;
	m_secondStartPhase = secondStartPhase;
	m_secondBlendDone = false;
	m_mirrored = false;
	m_newInstanceBehavior = FadeToStateParams::kUnspecified;
	m_customApRefId[0] = INVALID_STRING_ID_64;
	m_customApRefId[1] = INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimDoubleApBlendRequest::Start(NdGameObject& self)
{
	AnimControl* pAnimControl = self.GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	//Set the first apRef if its not valid
	if (!m_firstApRefValid)
	{
		CalculateFirstApRef(self);
	}
	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	if (m_apRef1Id != INVALID_STRING_ID_64)
	{
		Locator adjustedApRef0 = CalculateFirstApRefById(self);
		m_apRef[0].SetLocatorWs(adjustedApRef0);
		m_apRef[0].SetBinding(m_apRef[1].GetBinding());
	}

	FadeToStateParams params;
	params.m_stateStartPhase = m_startPhase;
	params.m_apRef = m_apRef[0];
	params.m_apRefValid = true;
	params.m_animFadeTime = m_fadeTime;
	params.m_motionFadeTime = m_motionFadeTime;
	params.m_freezeSrcState = m_freezeSrcState;
	params.m_freezeDestState = m_freezeDstState;
	params.m_blendType = m_srcBlendType;
	params.m_newInstBehavior = m_newInstanceBehavior;
	params.m_skipFirstFrameUpdate = m_preventFirstFrameUpdate;

	{
		bool useWarp = false;
		if (const DC::AnimState *pState = AnimActorFindState(pAnimControl->GetActor(), m_animStateId))
		{
			if (pState->m_tree && pState->m_tree->m_dcType == SID("anim-node-warper"))
			{
				useWarp = true;
			}
		}

		if (useWarp)
		{
			Locator apRefDifAnimSpace = m_apRef[0].GetLocatorWs().UntransformLocator(m_apRef[1].GetLocatorWs());
			params.m_apRef = m_apRef[1];
			params.m_warpApRefDif = apRefDifAnimSpace;
			params.m_haveWarpApRefDif = true;
			params.m_forceWarpApRefDif = true;
			m_secondBlendDone = true;
		}
	}

	params.m_customApRefId = m_customApRefId[0];
	pBaseLayer->FadeToState(m_animStateId, params);

	
	if (m_secondStartPhase <= 0.0f && !m_secondBlendDone)
	{
		FadeToStateParams params2;
		params2.m_stateStartPhase = m_startPhase;
		params2.m_apRef = m_apRef[1];
		params2.m_customApRefId = m_customApRefId[1];
		params2.m_apRefValid = true;
		params2.m_animFadeTime = m_doubleBlendTime;
		params2.m_motionFadeTime = m_doubleBlendTime;
		params2.m_freezeSrcState = false;
		params2.m_freezeDestState = m_freezeDstState;
		params2.m_blendType = m_dstBlendType;
		params2.m_newInstBehavior = FadeToStateParams::kUsePreviousTrack;
		params2.m_skipFirstFrameUpdate = m_preventFirstFrameUpdate;

		m_stateChangeId = pBaseLayer->FadeToState(m_animStateId, params2);
		m_secondBlendDone = true;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimDoubleApBlendRequest::UpdatePostAnim(NdGameObject& self)
{
	if (m_secondStartPhase > 0.0f && !m_secondBlendDone)
	{
		AnimControl* pAnimControl = self.GetAnimControl();
		AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
		ANIM_ASSERT(pBaseLayer != nullptr);

		if (pBaseLayer->CurrentStateInstance()->Phase() >= m_secondStartPhase)
		{
			FadeToStateParams params;
			params.m_stateStartPhase = m_secondStartPhase;
			params.m_animFadeTime = m_doubleBlendTime;
			params.m_apRef = m_apRef[1];
			params.m_customApRefId = m_customApRefId[1];
			params.m_apRefValid = true; 
			params.m_freezeDestState = m_freezeDstState;
			params.m_blendType = m_dstBlendType;
			params.m_newInstBehavior = FadeToStateParams::kUsePreviousTrack;
			params.m_skipFirstFrameUpdate = m_preventFirstFrameUpdate;

			m_stateChangeId = pBaseLayer->FadeToState(m_animStateId, params);
			m_secondBlendDone = true;
		}	
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimDoubleApBlendRequest::CalculateFirstApRef(NdGameObject& self)
{
	AnimControl* pAnimControl = self.GetAnimControl();

	StringId64 animId = m_animId;
	if (animId == INVALID_STRING_ID_64)
	{
		animId = FindAnimId(self);
	}
	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animId).ToArtItem();

	//ANIM_ASSERT(pAnim);

	Locator curAlign = self.GetLocator();
	Locator apRefWs(kIdentity);

	if (FindApReferenceFromAlign(self.GetSkeletonId(), pAnim, curAlign, &apRefWs, m_startPhase, m_mirrored))
	{
		switch (m_matchMode)
		{
		case kMatchAlignPosAndRot:
			{
				m_apRef[0].SetBinding(m_apRef[1].GetBinding());
				m_apRef[0].SetLocatorWs(apRefWs);
			}
			break;
		case kMatchBlendTransOnly: 
			{
				const Locator alignInApSpace = apRefWs.UntransformLocator(curAlign);
				const Locator startingAlign = m_apRef[1].GetLocator().TransformLocator(alignInApSpace);
				const Vector delta = curAlign.GetTranslation() - startingAlign.GetTranslation();
				m_apRef[0] = m_apRef[1];
				m_apRef[0].AdjustTranslation(delta);
			}
			break;
		case kMatchAlignPos: 
			{
				//We want to set the apRef such that the first frame of animation matches our current align, but if oriented so that the motion is more similar to the dest
				Vector alignToAnimAp = SafeNormalize(VectorXz(apRefWs.GetTranslation() - curAlign.GetTranslation()), GetLocalZ(curAlign.GetRotation()));
				Vector alignToDestAp = SafeNormalize(VectorXz(m_apRef[1].GetTranslation() - curAlign.GetTranslation()), GetLocalZ(curAlign.GetRotation()));

				Quat delta(QuatFromVectors(alignToAnimAp, alignToDestAp));
				ANIM_ASSERT(IsNormal(delta));
				Locator finalAlign( curAlign.GetTranslation(), curAlign.GetRotation()*delta);
				Locator finalApRefWs(finalAlign.TransformLocator(curAlign.UntransformLocator(apRefWs)));
				finalApRefWs.SetRotation(Normalize(finalApRefWs.GetRotation()));
				
				m_apRef[0].SetBinding(m_apRef[1].GetBinding());
				m_apRef[0].SetLocatorWs(finalApRefWs);

				break;
			}
		}
	}
	else
	{
		if (m_matchMode != kNone)
			m_apRef[0] = m_apRef[1];
	}

	m_firstApRefValid = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator AnimDoubleApBlendRequest::CalculateFirstApRefById(NdGameObject& self)
{	
	AnimControl* pAnimControl = self.GetAnimControl();

	if (m_animId == INVALID_STRING_ID_64)
	{
		m_animId = FindAnimId(self);
	}

	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(m_animId).ToArtItem();
		
	Locator outAlignWs;
	bool found = FindAlignFromApReference(self.GetSkeletonId(), pAnim, 0.0f, m_customAlign, m_apRef1Id, &outAlignWs, m_mirrored);

	if (!found)
	{
		outAlignWs = self.GetLocator();	// fix crash if missing anim.
#ifndef FINAL_BUILD
		MsgOut("DoubleApBlend Failed(FindAlignFromApRefernce), AnimId:%s ApName:%s\n", DevKitOnly_StringIdToString(m_animId), DevKitOnly_StringIdToString(m_apRef1Id));
#endif
	}

	// now we have the align, we can get the apReference from this align.
	Locator outApRefWs(kIdentity);
	found = FindApReferenceFromAlign(self.GetSkeletonId(), pAnim, outAlignWs, &outApRefWs, 0.0f, m_mirrored);


	if (!found)
	{
		outApRefWs = self.GetLocator();
		MsgOut("DoubleApBlend Failed(FromApReferenceFromAlign), AnimId:%s ApName:%s\n",
			   DevKitOnly_StringIdToString(m_animId),
			   DevKitOnly_StringIdToString(m_apRef1Id));
	}

	return outApRefWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimDoubleApBlendRequest::FindAnimId(NdGameObject& self) const
{
	StringId64 animId = INVALID_STRING_ID_64;
	AnimControl* pAnimControl = self.GetAnimControl();
	const DC::AnimState* pAnimState = AnimActorFindState(pAnimControl->GetActor(), m_animStateId);
	//if this isnt true we need to do a full snapshot to figure out what it will be			
	if (pAnimState->m_phaseAnimFunc == nullptr)
	{
		animId = pAnimState->m_phaseAnimName;
	}
	else
	{	
		//Don't do this for e3
		/*
		ScopedTempAllocator jj(FILE_LINE_FUNC);				
		const DC::AnimInfoCollection* pInfoCollection = pAnimControl->GetInfoCollection();				
		const AnimTable* pAnimTable = &pAnimControl->GetAnimTable();
		AnimStateSnapshot stateSnapshot;
		stateSnapshot.Init(NDI_NEW (kAlign16) AnimNodeSnapshot[10], 10);				
		stateSnapshot.SnapshotAnimState(pAnimState, nullptr, pAnimControl->GetAnimOverlaySnapshot(), nullptr, pAnimTable, pInfoCollection);
		animId = stateSnapshot.m_translatedPhaseAnimName;
		*/
	}

	return animId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Upright AP blend
AnimUprightApBlendRequest::AnimUprightApBlendRequest(StringId64 animStateId,
													 const BoundFrame& apRef1,
													 float fadeTime,
													 float motionFadeTime,
													 float doubleBlendTime)
{
	m_animStateId = animStateId;
	m_apRef = apRef1;
	
	m_fadeTime = fadeTime;
	m_motionFadeTime = motionFadeTime;
	m_uprightBlendTime = doubleBlendTime;
	m_newInstanceBehavior = FadeToStateParams::kUnspecified;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimUprightApBlendRequest::Start(NdGameObject& self)
{
	AnimControl* pAnimControl = self.GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	FadeToStateParams params;
	params.m_apRef = m_apRef;
	params.m_apRefValid = true;
	params.m_animFadeTime = m_fadeTime;
	params.m_motionFadeTime = m_motionFadeTime;

	pBaseLayer->FadeToState(m_animStateId, params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimUprightApBlendRequest::UpdatePreAnim(NdGameObject& self)
{
	AnimControl* pAnimControl = self.GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	if ((pBaseLayer->CurrentStateId() != m_animStateId))
		return;
	F32 phase = (pBaseLayer->CurrentStateId() == m_animStateId) ? pBaseLayer->CurrentStateInstance()->Phase() : 0.0f;

	Quat uprightLerp = m_apRef.GetRotation();
	uprightLerp = LevelQuat(uprightLerp);

	F32 tt = phase / m_uprightBlendTime;

	Quat lerped = Slerp(m_apRef.GetRotation(), uprightLerp, tt);

	lerped = Normalize(lerped);

	m_apRef.SetRotation(lerped);
	
	pBaseLayer->SetApRefOnCurrentState(m_apRef);
}
