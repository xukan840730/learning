/*7
 * Copyright (c) 2014 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */
#include "gamelib/gameplay/leg-ik/anim-ground-plane.h"

#include "corelib/math/intersection.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/level/artitem.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data-eval.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/anim/nd-anim-util.h"

struct AnimGroundPlane
{
	AnimGroundPlane()
		: m_planeNorm(kUnitYAxis)
		, m_alignHeight(0.0f) 
	{}

	Vector m_planeNorm;
	float m_alignHeight;
};

AnimGroundPlane Lerp( const AnimGroundPlane& leftData, const AnimGroundPlane& rightData, float animFade )
{
	AnimGroundPlane result;
	result.m_alignHeight = Lerp(leftData.m_alignHeight, rightData.m_alignHeight, animFade);
	result.m_planeNorm = Slerp(leftData.m_planeNorm, rightData.m_planeNorm, animFade);
	return result;
}

AnimGroundPlane GetGroundPlaneForAnim( const ArtItemAnim* pAnim, float phase, bool flipped )
{
	Locator grounLoc(kIdentity);
	bool eval = EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("apReference-ground"), phase, &grounLoc, flipped);
	AnimGroundPlane result;
	if (eval)
	{
		result.m_planeNorm = GetLocalY(grounLoc);
		Point alignProj(kOrigin);
		LinePlaneIntersect(grounLoc.GetTranslation(), result.m_planeNorm, Point(0.0f, 100.0f, 0.0f), Point(0.0f, -100.0f, 0.0f), nullptr, &alignProj);
		result.m_alignHeight = -alignProj.Y();
	}

	return result;
}

AnimGroundPlane GetGroundPlaneForAnim( const ArtItemAnim* pAnim, float phase, const AnimStateSnapshot* pSnapshot )
{
	return GetGroundPlaneForAnim(pAnim, phase, pSnapshot->IsFlipped());
}

Plane GetAnimatedGroundPlane(const ArtItemAnim* pAnim, float phase, bool flipped)
{
	AnimGroundPlane animGroundPlane = GetGroundPlaneForAnim( pAnim, phase, flipped);
	return Plane(Point(0.0f, -animGroundPlane.m_alignHeight, 0.0f), animGroundPlane.m_planeNorm);
}

class GroundPlaneAnimEval : public IAnimDataEval
{
public:
	class GroundPlaneAnimData : public IAnimDataEval::IAnimData
	{
	public:
		GroundPlaneAnimData(const AnimGroundPlane& groundPlane)
			: m_groundPlane(groundPlane)
		{
			ASSERT(IsFinite(m_groundPlane.m_alignHeight));
			ASSERT(IsFinite(m_groundPlane.m_planeNorm));
		}
		AnimGroundPlane m_groundPlane;
	};

	GroundPlaneAnimEval()
	{}

	virtual IAnimData* EvaluateDataFromAnim( const ArtItemAnim* pAnim, float phase, const AnimStateSnapshot* pSnapshot ) override
	{
		if (pAnim)
		{
			return NDI_NEW  GroundPlaneAnimData(GetGroundPlaneForAnim( pAnim, phase, pSnapshot ));
		}
		return nullptr;
	}

	virtual IAnimData* Blend( IAnimData* pLeft, IAnimData* pRight, float blend ) const override
	{
		const GroundPlaneAnimData* pLeftPlane = static_cast<const GroundPlaneAnimData*>(pLeft);
		const GroundPlaneAnimData* pRightPlane = static_cast<const GroundPlaneAnimData*>(pRight);

		if (pLeftPlane && pRightPlane)
		{
			return NDI_NEW GroundPlaneAnimData(Lerp(pLeftPlane->m_groundPlane, pRightPlane->m_groundPlane, blend));
		}
		else if (pLeftPlane)
		{
			return pLeft;
		}
		else if (pRightPlane)
		{
			return pRight;
		}
		return nullptr;
	}
};


class GroundPlaneAnimInstanceBlender : public AnimStateLayer::InstanceBlender<AnimGroundPlane>
{
	virtual AnimGroundPlane GetDefaultData() const override
	{
		return AnimGroundPlane();
	}

	virtual bool GetDataForInstance( const AnimStateInstance* pInstance, AnimGroundPlane* pDataOut ) override
	{
		ScopedTempAllocator jj(FILE_LINE_FUNC);
		GroundPlaneAnimEval animEval;
		IAnimDataEval::IAnimData* pData = pInstance->GetAnimStateSnapshot().EvaluateTree(&animEval);
		if (pData)
		{
			*pDataOut = static_cast<GroundPlaneAnimEval::GroundPlaneAnimData*>(pData)->m_groundPlane;
		}
		else
		{
			*pDataOut = AnimGroundPlane();
		}
		return true;
	}

	virtual AnimGroundPlane BlendData( const AnimGroundPlane& leftData, const AnimGroundPlane& rightData, float masterFade, float animFade, float motionFade ) override
	{
		return Lerp(leftData, rightData, animFade);
	}
};

Plane GetAnimatedGroundPlane(const NdGameObject* pGo)
{
	PROFILE(Processes, GetAnimatedGroundPlane);
	AnimGroundPlane groundPlane = GroundPlaneAnimInstanceBlender().BlendForward(pGo->GetAnimControl()->GetBaseStateLayer(), AnimGroundPlane());

	const Locator& loc = pGo->GetLocator();
	return Plane(loc.GetTranslation() + Vector(kUnitYAxis)*-groundPlane.m_alignHeight, loc.TransformVector(groundPlane.m_planeNorm));
}


Plane GetAnimatedGroundPlane( const Character* pCharacter )
{
	PROFILE(Processes, GetAnimatedGroundPlane);
	AnimGroundPlane groundPlane = GroundPlaneAnimInstanceBlender().BlendForward(pCharacter->GetAnimControl()->GetBaseStateLayer(), AnimGroundPlane());

	const Locator& loc = pCharacter->GetLocator();
	return Plane(loc.GetTranslation() + Vector(kUnitYAxis)*-groundPlane.m_alignHeight,loc.TransformVector(groundPlane.m_planeNorm));
}

Plane GetAnimatedGroundPlane( const AnimStateSnapshot &stateSnapshot, Locator alignWs)
{
	ScopedTempAllocator jj(FILE_LINE_FUNC);
	GroundPlaneAnimEval animEval;
	AnimGroundPlane groundPlane;
	IAnimDataEval::IAnimData* pData = stateSnapshot.EvaluateTree(&animEval);
	if (pData)
	{
		groundPlane = static_cast<GroundPlaneAnimEval::GroundPlaneAnimData*>(pData)->m_groundPlane;
	}
	return Plane(alignWs.GetTranslation() + Vector(kUnitYAxis)*-groundPlane.m_alignHeight,alignWs.TransformVector(groundPlane.m_planeNorm));
}

