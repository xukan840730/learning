/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/util/maybe.h"
#include "ndlib/util/tracker.h"

#include "gamelib/feature/feature-db-ref.h"
#include "gamelib/gameplay/leg-ik/ik-ground-model.h"
#include "gamelib/gameplay/leg-ik/leg-ik.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimStateSnapshot;
class ArtItemSkeleton;
class Character;
class CharacterLegIkController;

namespace DC
{
	struct AnimInfoCollection;
	struct LegIkFootDef;
}

namespace ndanim
{
	struct JointHierarchy;
}

class LegIkPlatform
{
public:
	LegIkPlatform()
	{
		Reset();
	}

	void Init(Point_arg refPt, 
			const EdgeInfo& edge, 
			float width, 
			bool rotateFeet, 
			bool disableSpring,
			SpringTracker<Vector>* pTracker, 
			Vector* pOffset);
	void Init(Point_arg refPt,
			Point_arg edgePt0,
			Point_arg edgePt1,
			float width,
			bool rotateFeet,
			bool disableSpring, 
			SpringTracker<Vector>* const pTracker,
			Vector* pOffset);
	bool IsValid() const { return m_width >= 0.0f && m_blend > 0.0f; }
	void DebugDraw(const Point_arg refPt) const;

	void Reset()
	{
		m_width = -1.0f;
		m_rotateFeet = false;
		m_disableSpring = false;
		m_pTracker = nullptr;
		m_pOffset = nullptr;
	}

public:
	Plane m_base;
	float m_width = -1.0f;
	float m_blend = 1.0f;
	bool m_rotateFeet = false;
	bool m_disableSpring = false;
	SpringTracker<Vector>* m_pTracker;
	Vector* m_pOffset;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class MoveLegIkNew : public ILegIk
{
protected:
	ndjob::CounterHandle m_pJobCounter;

	bool m_legOnGround[kQuadLegCount];

	float m_footstepPhase[kQuadLegCount];
	float m_footstepPlantTime[kQuadLegCount];
	BoundFrame m_footstepAnkleLoc[kQuadLegCount];
	BoundFrame m_footstepAlignLoc[kQuadLegCount];

	BoundFrame m_lastRootBase;
	bool m_lastRootBaseValid;

	GroundModel m_groundModel[kQuadLegCount];
	GroundModel m_alignGround;

	bool m_ankleSpaceValid;
	Locator m_footRefPointAnkleSpace[kQuadLegCount];

	BoundFrame m_prevUsedGroundPt[kQuadLegCount];
	bool m_prevUsedGroundPtValid[kQuadLegCount];

	Point m_groundModelStartPos[kQuadLegCount];
	Point m_alignGroundModelStartPos;
	Point m_alignGroundModelEndPos;
	Point m_alignPos;

	Point m_curSlatPos[kQuadLegCount];
	F32 m_slatBlend[kQuadLegCount];
	Vector m_rootXzOffset;
	SpringTracker<Vector> m_rootXzOffsetTracker;

	Vector m_smoothedGroundNormals[kQuadLegCount];
	SpringTracker<Vector> m_groundNormalSpringWs[kQuadLegCount];

	int m_nextFootplant;

	bool m_singleFrameDisableLeg[kQuadLegCount] = { false, false, false, false };

	Vector m_ankleAxis[kQuadLegCount];

	static int GetFootJointIds(FootIkCharacterType charType, const StringId64** pOutArray);
	static int GetFallbackFootJointIds(FootIkCharacterType charType, const StringId64** pOutArray);
	

	bool m_forceRootShift = false;
	F32 m_rootShift = 0.0f;

public:
	MoveLegIkNew();
	~MoveLegIkNew() override;

	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void Start(Character* pCharacter) override;
	static void SmoothLegGroundPositions(Character* pCharacter,
										 CharacterLegIkController* pController,
										 Point& legGroundPt,
										 int legIndex);
	Locator ApplyLegToGround(Character *const pCharacter,
							CharacterLegIkController *const pController,
							const Point_arg animGroundPosWs,
							const Vector_arg animGroundNormalWs,
							const DC::LegIkFootDef *const pLegIkFootDef, 
							const LegIkPlatform& legIkPlatform,
							int legIndex,
							const Locator& startingAnkleLocWs,
							const Locator& startingHeelLocWs, 
							bool& outOnGround,
							Vector& outRootXzOffset);
	Vector LimitNormalAngle(Vector_arg normal, Vector_arg animationNormal);

	void SetRootShift(F32 shift) { m_forceRootShift = true; m_rootShift = shift; }
	Vector m_footNormals[kQuadLegCount];

	Quat AdjustFootNormal(Character* pCharacter,
						  CharacterLegIkController* pController,
						  Vector normal,
						  int legIndex,
						  float blend,
						  Vector_arg animationGroundNormal,
						  Quat_arg animAnkleRot);
	void Update(Character* pCharacter, CharacterLegIkController* pController, bool doCollision) override;

	void SetLastRootBase(Character* pCharacter, CharacterLegIkController* pController);

	void PostAnimUpdate(Character* pCharacter, CharacterLegIkController* pController) override;

	virtual Maybe<Point> GetNextPredictedFootPlant() const override;
	virtual void GetPredictedFootPlants(Maybe<Point> (&pos)[kQuadLegCount]) const override;

	virtual void SingleFrameDisableLeg(int legIndex) override;

	virtual const char* GetName() const override { return "MoveLegIkNew"; };

	CLASS_JOB_ENTRY_POINT_DEFINITION(FindGroundJob);
	CLASS_JOB_ENTRY_POINT_DEFINITION(UpdateFeetPlantedJob);

	GroundModel* GetGroundModel() { return &m_alignGround; }
	const GroundModel* GetGroundModel() const { return &m_alignGround; }

	Vector GetAnkleAxis(int legIndex) { ANIM_ASSERT(m_ankleSpaceValid); return m_ankleAxis[legIndex]; }

protected:
	void UpdateFeetPlanted(Character* pCharacter, bool reset, const Locator aLegPts[], int legCount);

	void DetermineNextFootStep();

	void FindFootPlantGround(bool inSnow);

	void UpdateNextFootsteps(Character* pCharacter,
							 AnimStateSnapshot& stateSnapshot,
							 float startPhase,
							 float endPhase,
							 const Locator initialLocWs,
							 const BoundFrame& apRef,
							 const ArtItemSkeleton* pArtItemSkel,
							 const DC::AnimInfoCollection* pInfo,
							 const ndanim::JointHierarchy* pJointHierarchy,
							 Binding characterBinding);

	virtual bool GetMeshRaycastPointsWs(Character* pCharacter, Point* pLeftLegWs, Point* pRightLegWs, Point* pFrontLeftLegWs, Point* pFrontRightLegWs) override;

	void InitAnkleSpace(Character* pCharacter);
};
