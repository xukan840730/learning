/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */
#ifndef FREEZE_LEG_IK_H
#define FREEZE_LEG_IK_H

#include "gamelib/gameplay/leg-ik/move-leg-ik.h"

class Character;
class CharacterLegIkController;

class FreezeLegIk : public MoveLegIk
{
private:
	Locator m_lockedPosOs[kQuadLegCount];

	bool m_desiredPosLocked;
	bool m_targetPosLocked;

public:
	virtual float GetBlend(CharacterLegIkController* pController) override;
	virtual void Start(Character* pCharacter) override;
	virtual void Update(Character* pCharacter, CharacterLegIkController* pController, bool doCollision) override;
	virtual void SingleFrameUnfreeze() override;
	virtual float GetClampedMin() override { return -0.3f; }
	virtual bool GetMeshRaycastPointsWs(Character* pCharacter, Point* pLeftLegWs, Point* pRightLegWs, Point* pFrontLeftLegWs, Point* pFrontRightLegWs) override;
	virtual const char* GetName() const override { return "FreezeLegIk"; };
};

#endif
