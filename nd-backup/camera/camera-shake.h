/*
* Copyright (c) 2003 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#ifndef CAMERASHAKE_H
#define CAMERASHAKE_H

#include "corelib/util/timeframe.h"
#include "corelib/math/locator.h"

class ProcessWeapon;
class EffectAnimInfo;

namespace DC
{
	struct ExplosionShake;
}

namespace DMENU
{
	class Menu;
}


class CameraShake
{
public:
	void Init (int playerIndex, Point cameraPos);

	Locator GetLocator(int playerIndex, Locator loc, float blend = 1.0f, bool allowAmbientShake = true) const;
	Locator GetLocator(int playerIndex, Point cameraPos, Point targetPos, Vector upVec, float blend = 1.0f, bool allowAmbientShake = true) const;

	float GetFov(int playerIndex, float fov, float blend = 1.0f);

	// specific camera shake functions
	static void DoAdditiveAnimationShake(int playerIndex, StringId64 id, StringId64 nameId, StringId64 animId, float blend, TimeFrame fadeTime,
		TimeFrame maxTimeWithoutReinit, bool isAmbientShake, float minContribute = 0.0f, bool reinit = true, float rate = 1.0f, bool canAbandon = false);
	static void DoAdditiveAnimationShake(int playerIndex, StringId64 id, StringId64 nameId, StringId64 animId, float blend, TimeFrame fadeTime, float blendFinal, TimeFrame blendFinalTime,
		TimeFrame maxTimeWithoutReinit, bool isAmbientShake, float minContribute = 0.0f, bool reinit = true, float rate = 1.0f, bool canAbandon = false);
	static void CancelAdditiveAnimationShake(int playerIndex, StringId64 id, TimeFrame fadeTime);
	static void AbandonAdditivesNotInList(int playerIndex, StringId64* activeShakeIds, int numActiveShakeIds);
	static void DoShootShake(int playerIndex);
	static void GetArrayOfNameIds(StringId64* nameIdArray, int maxNameIds, int& nameIdArraySize);
	static void DoShake(int playerIndex, float scale, float amplitude, TimeFrame time);
	static void DoNearMissShake(int playerIndex);
	static void DoGrenadeShake(int playerIndex, float scale);
	static void DoEffShake(int playerIndex, float scale, StringId64 animId = INVALID_STRING_ID_64, bool ambient = true);
	static void DoEffShake(int playerIndex, const EffectAnimInfo* pEffectAnimInfo);
	static void DoExplosionShake(int playerIndex, const DC::ExplosionShake* pShake, float dist);
	static void DoShotReactionShake(int playerIndex, float scale, float durationScale = 1.0f);
	static void AddDamageMovement(int playerIndex, Vec2 vel, float springConst, float maxDeflection);
	static void StartFlamethrowerShake();

	// camera shake system interface
	static void Update(int playerIndex);
	static void Reset(int playerIndex);
	static void CreateShakeMenu(DMENU::Menu* pMenu);

	static void SetAdditiveAnimationShakePhase(int playerIndex, StringId64 id, float phase);

	static float GetShakeBlendMultiplier();


	static void ScriptSetCameraShakeStrengthMultiplier(float strength);
	static float GetScriptedCameraShakeStrengthMultiplier();
	static I64 s_scriptedBlendMultiplierFrame;
	static float s_scriptedBlendMultiplier;

};

#endif
