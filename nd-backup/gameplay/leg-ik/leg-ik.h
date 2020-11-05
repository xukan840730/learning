/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */
#ifndef LEG_IK_H
#define LEG_IK_H

#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/util/maybe.h"

#include "gamelib/gameplay/leg-ik/foot-analysis.h"

class Character;
class LegIkChain;
class CharacterLegIkController;
struct LegIkInstance;


#ifndef FINAL_BUILD
#ifndef CHECK_LEG_INDEX
#define CHECK_LEG_INDEX(legIndex)                                                    \
do                                                                                   \
{                                                                                    \
	ANIM_ASSERTF(legIndex >= 0 && legIndex < m_legCount,                             \
		("Invalid leg index %d, legCount %d",                                        \
			legIndex, m_legCount));                                                  \
} while (0);                                                                                  
#endif //#ifndef CHECK_LEG_INDEX_CONTROLLER
#else
#ifndef CHECK_LEG_INDEX
#define CHECK_LEG_INDEX(legIndex)  
#endif // #else (#ifndef FINAL_BUILD
#endif //#ifndef FINAL_BUILD

//------------------------------------------------------------------------------------
// Leg IK implementation
//------------------------------------------------------------------------------------
class ILegIk
{
protected:
	LegIkChain *m_legIks[kQuadLegCount];
	int m_legCount;

public:
	float m_blend;

	class RootBaseSmoother : public FootAnalysis::IRootBaseSmoother
	{
	public:
		RootBaseSmoother(CharacterLegIkController* pController, float blend, float alignY, bool onStairs);
		float SmoothY(float desiredY, float maxY) override;

	private:
		CharacterLegIkController* m_pController;
		float m_blend;
		float m_alignY;
		bool m_onStairs;
	};

	ILegIk();
	virtual ~ILegIk() {}

	virtual void InitLegIk(Character* pCharacter, LegIkChain *pLegIks, int legCount);
	virtual void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound);

	virtual void Start(Character* pCharacter);
	virtual void Update(Character* pCharacter, CharacterLegIkController* pController, bool doCollision) = 0;
	virtual void PostAnimUpdate(Character* pCharacter, CharacterLegIkController* pController) {}
	virtual void Stop(Character* pCharacter) {}

	void SetBlend(float blend);
	void BlendIn(float blendSpeed);
	void BlendOut(float blendSpeed);
	virtual float GetClampedMin() { return -0.4f; }
	virtual float GetBlend(CharacterLegIkController* pController);
	virtual void SetupDefaultIkInfo(LegIkInstance* ik);
	virtual void SingleFrameDisableLeg(int legIndex);
	virtual void SingleFrameUnfreeze();

	virtual void AdjustRootPositionFromLegs(Character* pCharacter, CharacterLegIkController* pController, Point aLegs[], int legCount, float* pDesiredRootBaseY);
	// DOES NOT SUPPORT QUADRUPEDS
	virtual void AdjustRootPositionFromLegs_Simple(Character* pCharacter, CharacterLegIkController* pController, Point& leftLeg, Point& rightLeg);

	// aLegs will be modified by this function;
	virtual void DoLegIk(Character* pCharacter, CharacterLegIkController* pController, Locator aLegs[], int legCount, float* pDesiredRootOffset, Vector rootXzOffset);

	virtual float GetAdjustedRootDelta(const float desiredRootDelta) { return desiredRootDelta; }
	virtual void AdjustedRootLimits(float& minRootDelta, float& maxRootDelta) {}

	virtual void AdjustFeet(Point* pLeftLeg, Point* pRightLeg, Point* pFrontLeftLeg, Point* pFrontRightLeg) {}

	virtual bool GetMeshRaycastPointsWs(Character* pCharacter, Point* pLeftLegWs, Point* pRightLegWs, Point* pFrontLeftLegWs, Point* pFrontRightLegWs) { return false; }

	virtual Maybe<Point> GetNextPredictedFootPlant() const;
	virtual void GetPredictedFootPlants(Maybe<Point> (&pos)[kQuadLegCount]) const;
	virtual const char* GetName() const = 0;
private:
	virtual bool UseNewSolveTechnique() const;
};

//Ik Constants
extern float g_rootYSpeedSpring;
extern float g_rootYSpring;
extern float g_legYSpring;
extern float g_footNormalSpring;

#endif
