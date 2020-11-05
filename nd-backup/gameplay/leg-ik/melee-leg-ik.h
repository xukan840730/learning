/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */
#ifndef MELEE_LEG_IK_H
#define MELEE_LEG_IK_H

#include "gamelib/gameplay/character-leg-ik.h"
#include "gamelib/gameplay/leg-ik/move-leg-ik.h"
#include "ndlib/anim/joint-modifiers/joint-modifier-data.h"

class Character;
class CharacterLegIkController;


class MeleeLegIk : public MoveLegIk
{
private:
	float m_desiredRootDelta;
	float m_leftFootDelta;
	float m_rightFootDelta;
	float m_frontLeftFootDelta;
	float m_frontRightFootDelta;
	bool m_rootDeltaSet;
	bool m_feetDeltasSet;
	bool m_ikInfoValid;
	LegIkInfoShared m_ikInfo;

public:
	MeleeLegIk()
		: m_desiredRootDelta(0)
		, m_rootDeltaSet(false)
		, m_feetDeltasSet(false)
		, m_ikInfoValid(false)
	{}

	virtual void Start(Character* pCharacter) override;
	void SetRootShiftDelta(float delta);
	void SetFeetDeltas(float leftFootDelta, float rightFootDelta, float frontLeftFootDelta, float frontRightFootDelta);
	virtual float GetBlend(CharacterLegIkController* pController) override
	{
		//No slope blend
		return m_blend;
	}

	MeleeIkInfo GetMeleeIkInfo(Character* pCharacter) const;
	virtual float GetAdjustedRootDelta(const float desiredRootDelta) override;
	virtual void AdjustFeet(Point* pLeftLeg, Point* pRightLeg, Point* pFrontLeftLeg, Point* pFrontRightLeg) override;
	virtual void AdjustedRootLimits(float& minRootDelta, float& maxRootDelta) override;
	virtual void AdjustRootPositionFromLegs(Character* pCharacter, CharacterLegIkController* pController, Point aLegs[], int legCount, float* pDesiredRootBaseY) override;
	virtual void Update(Character* pCharacter, CharacterLegIkController* pController, bool doCollision) override;

	virtual const char* GetName() const override { return "MeleeLegIk"; };
private:
	bool UseNewSolveTechnique() const override {
		return false;
	}
};

#endif
