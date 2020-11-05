/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef FOOT_ANALYSIS_H
#define FOOT_ANALYSIS_H

#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/util/maybe.h"

class ArtItemAnim;
class LegIkChain;
class NdGameObject;

class FootAnalysis
{
public:
	struct FootBase
	{
		FootBase()
			:loc(kIdentity)
			, footLength(1.0f)
		{}
		Locator loc;
		float footLength;

		const Point GetHeelPoint() const { return loc.GetTranslation(); }
		const Point GetBallPoint() const { return loc.TransformPoint(Point(0.0f, 0.0f, footLength)); }
	};

	struct FootPlantPosition
	{
		Locator align;
		FootBase foot;
		float footHeightOffGround;
		Vector groundNormal;
	};

	struct FootPlantPositions
	{
		Maybe<FootPlantPosition> start;
		Maybe<FootPlantPosition> end;
	};

	struct FeetPlantPositions
	{
		FootPlantPositions m_plants[kQuadLegCount];
	};

	struct FootIkData
	{
		FootIkData() 
			: m_flightArc(0.0f)
			, m_groundWeight(1.0f)
		{}

		float m_flightArc;
		float m_groundWeight;
	};

	struct FeetIkData
	{
		FootIkData m_footData[kQuadLegCount];
	};

	class IFootPrediction
	{
	public:
		virtual ~IFootPrediction() {};
		virtual Point EvaluateGroundPoint(const FootBase& animFoot) = 0;
		virtual FootBase EvaluateFoot(const FootBase& animFoot) = 0;
	};

	class IRootBaseSmoother
	{
	public:
		virtual ~IRootBaseSmoother() {}

		virtual float SmoothY(float desiredY, float maxY) = 0;
	};

	static void AnalyzeFeet(const ArtItemAnim* pAnim, float phase, FootIkCharacterType charType);
	static void TestFootIk(NdGameObject* pGo, const ArtItemAnim* pAnim, float phase, bool mirror, bool enableCollision);
	static void DoFootIk(
		NdGameObject* pGo,
		const ArtItemAnim* pAnim,
		float phase,
		bool mirror,
		bool enableCollision,
		LegIkChain** apIkChains,			// must be an array of at least legCount length
		const Plane& animGroundPlaneWs,
		const Locator animSpaceToWorldSpace,
		const FeetIkData& feetIkData,
		IFootPrediction** apPredictions,	// must be an array of at least legCount length
		IRootBaseSmoother* pRootSmoother,
		int legCount);

	static F32 SolveLegIk(
		const Locator& align,
		LegIkChain** apIkChains,		  // must be an array of at least legCount length
		Locator* aDesiredAnkleLocs,		  // must be an array of at least legCount length
		float* aFlightArcs,				  // must be an array of at least legCount length
		IRootBaseSmoother* pRootSmoother, // must be an array of at least legCount length
		FootIkCharacterType charType,
		int legCount,
		F32 enforceHipDist = 0.0f,
		bool moveRootUp = false,
		Vector rootXzOffset = Vector(kZero),
		float* pDesiredRootOffset = nullptr);

	static FeetIkData GetFootData(NdGameObject* pGo);
	static FeetIkData GetFeetDataFromAnim(const ArtItemAnim* pAnim, float phase, bool mirror, FootIkCharacterType charType);
	static FeetPlantPositions GetFootPlantPositionsFromAnimWs(const ArtItemAnim* pAnim, float phase, bool mirror, const Locator& animSpaceToWorldSpace, FootIkCharacterType charType);
	static IFootPrediction* CreateSimpleFootPrediction(const FootPlantPositions& plants);
	static FeetPlantPositions DebugCastFeetToGround(const FeetPlantPositions& plants);
	//static FootPlantPosition DebugCastFootToGround(const FootPlantPosition& plant);

	static bool s_debugDraw;
};

#endif //FOOT_ANALYSIS_H
