/*
* Copyright (c) 2015 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/
#ifndef LEG_ARM_IK_H
#define LEG_ARM_IK_H

#include <Eigen/Dense>

class ArmIkChain;
class Character;
class CharacterLegIkController;

//------------------------------------------------------------------------------------
// Arm IK implementation
//------------------------------------------------------------------------------------
class IArmIk
{
protected:
	ArmIkChain *m_armIks[2];

public:
	float m_handBlend[2];
	float m_blend;

	IArmIk();
	virtual ~IArmIk() {}

	virtual void InitArmIk(Character* pCharacter, ArmIkChain *pArmIks);
	virtual void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound);

	virtual void Start(Character* pCharacter);
	virtual void Update(Character* pCharacter, CharacterLegIkController* pController) = 0;
	virtual void PostAnimUpdate(Character* pCharacter, CharacterLegIkController* pController) {}
	virtual void Stop(Character* pCharacter) {}

	void SetBlend(float blend);
	void BlendIn(float blendSpeed);
	void BlendOut(float blendSpeed);
	virtual float GetBlend(CharacterLegIkController* pController);

	virtual void SingleFrameUnfreeze();
	virtual void DoArmIk(Character* pCharacter, CharacterLegIkController* pController, const Locator *pHandLoc, const bool *pHandEvaluated);
	virtual const char* GetName() const = 0;
private:
};

#endif
