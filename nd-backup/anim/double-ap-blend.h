/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-state-layer.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NdGameObject;

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimDoubleApBlendRequest
{
	enum EAlignMatchMode
	{ 
		kNone,
		kMatchAlignPos,
		kMatchAlignPosAndRot,
		kMatchBlendTransOnly
	};

	void CalculateFirstApRef(NdGameObject& self);
	Locator CalculateFirstApRefById(NdGameObject& self);
	StringId64 FindAnimId(NdGameObject& self) const;

	void Start(NdGameObject& self);
	void UpdatePostAnim(NdGameObject& self);

	StringId64 m_animStateId;	
	StringId64 m_animId; //needs to be set for animStates with phase anim funcs
	float m_fadeTime;
	float m_motionFadeTime;
	float m_doubleBlendTime;
	float m_startPhase;
	StateChangeRequest::ID m_stateChangeId;
	BoundFrame m_apRef[2];
	StringId64 m_customApRefId[2];
	bool m_firstApRefValid;
	EAlignMatchMode m_matchMode;
	bool m_freezeSrcState;
	bool m_freezeDstState; //This flag isnt really supported...
	bool m_mirrored;
	bool m_preventFirstFrameUpdate;
	DC::AnimCurveType m_srcBlendType;
	DC::AnimCurveType m_dstBlendType;
	StringId64 m_apRef1Id;
	Locator m_customAlign;
	float m_secondStartPhase;
	bool m_secondBlendDone;
	FadeToStateParams::NewInstanceBehavior m_newInstanceBehavior;

	AnimDoubleApBlendRequest(StringId64 animStateId,
							 const BoundFrame& apRef1,
							 const BoundFrame& apRef2,
							 float fadeTime,
							 float motionFadeTime,
							 float doubleBlendTime,
							 float startPhase = 0.0f,
							 DC::AnimCurveType srcBlendType = DC::kAnimCurveTypeUniformS,
							 DC::AnimCurveType dstBlendType = DC::kAnimCurveTypeLinear);
	AnimDoubleApBlendRequest(StringId64 animStateId,
							 EAlignMatchMode matchMode,
							 const BoundFrame& apRef2,
							 float fadeTime,
							 float motionFadeTime,
							 float doubleBlendTime,
							 float startPhase = 0.0f,
							 DC::AnimCurveType srcBlendType = DC::kAnimCurveTypeUniformS,
							 DC::AnimCurveType dstBlendType = DC::kAnimCurveTypeLinear);
	AnimDoubleApBlendRequest(StringId64 animStateId,
							 StringId64 apRef1Id,
							 const Locator& customAlign,
							 const BoundFrame& apRef2,
							 float fadeTime,
							 float motionFadeTime,
							 float doubleBlendTime,
							 float startPhase		= 0.f,
							 float secondStartPhase = 0.f,
							 DC::AnimCurveType srcBlendType = DC::kAnimCurveTypeUniformS,
							 DC::AnimCurveType dstBlendType = DC::kAnimCurveTypeLinear);
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimUprightApBlendRequest
{
	StringId64 m_animStateId;
	float m_fadeTime;
	float m_motionFadeTime;
	float m_uprightBlendTime;
	
	StateChangeRequest::ID m_stateChangeId;
	BoundFrame m_apRef;
	
	FadeToStateParams::NewInstanceBehavior m_newInstanceBehavior;

	AnimUprightApBlendRequest(StringId64 animStateId,
							  const BoundFrame& apRef1,
							  float fadeTime,
							  float motionFadeTime,
							  float uprightBlendTime);

	StringId64 FindAnimId(NdGameObject& self) const;

	void Start(NdGameObject& self);
	void UpdatePreAnim(NdGameObject& self);
};
