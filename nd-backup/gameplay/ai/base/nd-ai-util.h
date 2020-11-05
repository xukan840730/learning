/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/ai/agent/nav-character-adapter.h"
#include "gamelib/gameplay/ai/exposure-map.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/scriptx/h/nd-ai-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimStateInstance;
class ArtItemAnim;
class NavPoly;
class NdAnimationControllers;
class NdGameObject;

namespace DC
{
	struct SelfBlendParams;
}

namespace NdAnimAlign
{
	class InstanceAlignTable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
namespace AI
{
	const DC::AiLibGlobalInfo* GetAiLibGlobalInfo();

	bool AdjustMoveToNavMeshPs(const NavMesh* pNavMesh,
							   const NavPoly* pStartPoly,
							   Point_arg startPosPs,
							   Point_arg desiredPosPs,
							   const Segment& probePs,
							   float moveRadius,
							   float maxMoveDist,
							   const Nav::StaticBlockageMask obeyedStaticBlockerMask,
							   const NavMesh::NpcStature minNpcStature,
							   const NavBlockerBits& obeyedBlockers,
							   Point* pReachedPosPsOut,
							   bool debugDraw = false);

	Point AdjustPointToNavMesh(const NavCharacterAdapter& pNavChar,
							   const NavMesh* pNavMesh,
							   const NavPoly* pStartPoly,
							   Point_arg startingPosPs,
							   NavMesh::NpcStature minNpcStature,
							   float adjRadius,
							   float maxMoveDist);

	// Animation helpers
	void SetPluggableAnim(const NdGameObject* pNavChar, const StringId64 animId, const bool mirror = false);
	void SetPluggableFaceAnim(const NdGameObject* pNavChar, const StringId64 animId);

	F32 RandomRange(const DC::AiRangeval* pFloatRange);
	I32 RandomRange(const DC::AiRangevalInt* pIntRange);
	inline F32 RandomRange(const DC::AiRangeval& floatRange) { return AI::RandomRange(&floatRange); }
	inline I32 RandomRange(const DC::AiRangevalInt& intRange) { return AI::RandomRange(&intRange); }

	struct AnimClearMotionDebugOptions
	{
		AnimClearMotionDebugOptions(bool draw = false,
									float offsetY = 0.1f,
									DebugPrimTime duration = kPrimDuration1FramePauseable)
			: m_draw(draw), m_offsetY(offsetY), m_duration(duration)
		{
		}

		bool m_draw;
		float m_offsetY;
		DebugPrimTime m_duration;
	};

	bool AnimHasClearMotion(const SkeletonId skelId,
							const ArtItemAnim* pAnim,
							const Locator& initialApRefWs,
							const Locator& rotatedApRefWs,
							const DC::SelfBlendParams* pSelfBlend,
							const NavMesh* pStartMesh,
							const NavMesh::ProbeParams& baseParams,
							bool useCurrentAlignInsteadOfApRef = false,
							const AnimClearMotionDebugOptions& debugOptions = AnimClearMotionDebugOptions());

	StringId64 NavMeshForcedDemeanor(const NavMesh* pMesh);

	bool NavPolyForcesWade(const NavPoly* pPoly);

	void CalculateLosPositionsWs(Point_arg fromPosWs,
								 Point_arg basePosWs,
								 Point* centrePosWs,
								 Point* rightPosWs,
								 Point* leftPosWs);

	F32 TrackSpeedFactor(const NdGameObject* pNavChar, F32 curValue, F32 desValue, F32 filter);

	void GetExposureParamsFromMask(DC::ExposureSourceMask exposureMask,
								   DC::ExposureSource& exposureMapSource,
								   ExposureMapType& exposureMapType);

}

/// --------------------------------------------------------------------------------------------------------------- ///
namespace NavUtil
{
	void PropagateAlignRestriction(NdGameObject* pGameObj,
								   Point_arg adjustedPos,
								   Vector_arg adjustmentVec,
								   const NdAnimAlign::InstanceAlignTable& alignTable);

	I32F FindPathSphereIntersection(const IPathWaypoints* pPathPs,
									Point_arg searchOriginPs,
									F32 radius,
									Point* pPosPsOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
class NoAdjustToNavMeshFlagBlender : public AnimStateLayer::InstanceBlender<float>
{
public:
	NoAdjustToNavMeshFlagBlender(const NavCharacterAdapter& nca) : m_charAdapter(nca) {}
	virtual ~NoAdjustToNavMeshFlagBlender() override {}

	virtual float GetDefaultData() const override { return 0.0f; }
	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, float* pDataOut) override;

	virtual float BlendData(const float& leftData,
							const float& rightData,
							float masterFade,
							float animFade,
							float motionFade) override;

private:
	bool GetTapFactor(F32* pDataOut) const;
	bool GetCapFactor(F32* pDataOut) const;

	const NavCharacterAdapter& m_charAdapter;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NoAdjustToGroundFlagBlender : public AnimStateLayer::InstanceBlender<float>
{
public:
	NoAdjustToGroundFlagBlender(const NavCharacterAdapter& nca) : m_charAdapter(nca) {}
	virtual ~NoAdjustToGroundFlagBlender() override {}

	virtual float GetDefaultData() const override { return 0.0f; }
	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, float* pDataOut) override;
	virtual float BlendData(const float& leftData,
							const float& rightData,
							float masterFade,
							float animFade,
							float motionFade) override;

private:
	const NavCharacterAdapter& m_charAdapter;
};

/// --------------------------------------------------------------------------------------------------------------- ///
float GetGroundAdjustFactorForInstance(const AnimStateInstance* pInstance, const NavCharacterAdapter& charAdapter);

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsKnownScriptedAnimationState(StringId64 stateId);
