/*
* Copyright (c) 2003 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/camera/camera-shake.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/camera/camera-final.h"
#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/io/package-mgr.h"
#include "ndlib/nd-config.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/settings/priority.h"
#include "ndlib/anim/effect-anim-entry-tag.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/scriptx/h/dc-types.h"
#include "ndlib/util/curves.h"
#include "ndlib/settings/settings.h"

#include "gamelib/camera/camera-manager.h"
#include "gamelib/camera/damage-camera-shake.h"
#include "gamelib/io/rumblemanager.h"
#include "gamelib/level/artitem.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/scriptx/h/dynamic-light.h"
#include "gamelib/scriptx/h/nd-camera-settings.h"
#include "gamelib/state-script/ss-manager.h"
#include "gamelib/script/nd-script-arg-iterator.h"
#include "ndlib/anim/effect-group.h"

bool g_cameraShakeDamping = false;
float g_cameraShakeDampingStartAngle = 2.0f;
float g_cameraShakeDampingEndAngle = 2.0f;
float g_cameraShakeDampingPercent = 0.5f;

I64 CameraShake::s_scriptedBlendMultiplierFrame = -1000;
float CameraShake::s_scriptedBlendMultiplier = 1.0f;

//----------------------------------------------------------------------------------------------------------------------
// Camera Shake implementation
//----------------------------------------------------------------------------------------------------------------------
class CameraAdditive 
{
public:
	virtual Locator GetLocator(const Locator& loc, float blend) = 0;
	virtual float GetFov(const float& fov, float blend) { return fov; }
	virtual bool IsDone() = 0;
	virtual void Update() {}
	virtual bool IsAmbientShake() { return false; }
};

//----------------------------------------------------------------------------------------------------------------------
// Procedural Shake
//----------------------------------------------------------------------------------------------------------------------
class ShakeObject : public CameraAdditive
{
public:
	// Base shake type 
	virtual void Init(float timeScale, float amplitude, TimeFrame runTime)
	{
		m_timeScale = timeScale;
		m_amplitude = amplitude;
		m_runTime	= TimeDelta(runTime);
		m_startTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime();
		m_offset[0] = frand(0.0f, 2.0f * PI);
		m_offset[1] = frand(0.0f, 2.0f * PI);
		m_offset[2] = frand(0.0f, 2.0f * PI);
	}

	virtual Locator GetLocator(const Locator& loc, float blend) override
	{
		float time = ToSeconds(EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime() - m_startTime);

		float timeScale = QuadCurveEaseOutDown.Evaluate(time / ToSeconds(m_runTime));

		time *= m_timeScale;

		timeScale *= m_amplitude;

		Quat q = loc.Rot();

		float zAngle = timeScale * Sin(time * PI * 3.75f + DEGREES_TO_RADIANS(270.0f) + m_offset[0]);
		float xAngle = timeScale * Sin(time * PI * 3.75f + DEGREES_TO_RADIANS(22.5f) + m_offset[1]);
		float yAngle = timeScale * Sin(time * PI * 2.00f + DEGREES_TO_RADIANS(180.0f) + m_offset[2]);
		q *= QuatFromAxisAngle(Vector(SMath::kUnitYAxis), DEGREES_TO_RADIANS(0.50f * yAngle));
		q *= QuatFromAxisAngle(Vector(SMath::kUnitXAxis), DEGREES_TO_RADIANS(0.50f * xAngle));
		q *= QuatFromAxisAngle(Vector(SMath::kUnitZAxis), DEGREES_TO_RADIANS(0.25f * zAngle));
		q = SafeNormalize(q, Quat(SMath::kIdentity));

		Locator newLoc = loc;
		newLoc.SetRot(q);

		return Lerp(loc, newLoc, blend);
	}

	virtual bool IsDone() override
	{
		return EngineComponents::GetNdFrameState()->GetClock(kGameClock)->TimePassed(m_runTime, m_startTime);
	}

protected:
	TimeFrame m_startTime;
	TimeDelta m_runTime;
	float m_offset[3];
	float m_timeScale;
	float m_amplitude;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class CameraAnimationAdditive : public CameraAdditive
{
private:
	StringId64 m_id;
	StringId64 m_animId;
	StringId64 m_nameId;
	SkeletonId m_skelId;
	U32 m_hierarchyId;
	float m_phase;
	float m_rate;
	float m_finalBlend;
	bool m_canAbandon;
	bool m_blendingOut;
	TimeFrame m_startTime;
	TimeFrame m_endTime;
	TimeFrame m_fadeInTime;
	TimeFrame m_fadeOutTime;
	SpringTracker<float> m_blendSpring;
	float m_blend;
	float m_targetBlend;
	float m_blendFinal;
	TimeFrame m_blendFinalTime;
	float m_minContribute;
	bool m_isAmbientShake;

	void Advance()
	{
		const ArtItemAnim* anim = AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, m_animId).ToArtItem();
		if (anim == nullptr)
			return;

		m_phase = FromUPS(anim->m_pClipData->m_framesPerSecond * anim->m_pClipData->m_phasePerFrame*m_rate) + m_phase;

		if (IsLooping())
		{
			m_phase = fmodf(m_phase, 1.0f);	
		}
		else
		{
			m_phase = Min(1.0f, m_phase);
		}
	}

	bool IsLooping()
	{
		const ArtItemAnim* anim = AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, m_animId).ToArtItem();
		if (anim == nullptr)
			return false;

		return anim->m_flags & ArtItemAnim::kLooping;
	}

	Locator GetIntialAnimationLocator()
	{
		ndanim::JointParams out;
		Locator loc(kIdentity);
		if (GetInitialAnimationJointParams(out))
		{
			loc.SetPos(-out.m_trans);
			loc.SetRot(out.m_quat * QuatFromAxisAngle(Vector(SMath::kUnitYAxis), DEGREES_TO_RADIANS(-180.0f)));
		}

		return loc;
	}

	Locator GetCurrentAnimationLocator()
	{
		ndanim::JointParams out;
		Locator loc(kIdentity);
		if (GetCurrentAnimationJointParams(out))
		{
			loc.SetPos(-out.m_trans);
			loc.SetRot(out.m_quat * QuatFromAxisAngle(Vector(SMath::kUnitYAxis), DEGREES_TO_RADIANS(-180.0f)));
		}

		return loc;
	}

	bool GetInitialAnimationJointParams(ndanim::JointParams& jointParams)
	{
		return GetCurrentJointParams(jointParams, 0.0f);		
	}

	bool GetCurrentAnimationJointParams(ndanim::JointParams& jointParams)
	{
		return GetCurrentJointParams(jointParams, m_phase);
	}

	bool GetCurrentJointParams(ndanim::JointParams& jointParams, float phase)
	{
		const ArtItemAnim* pAnim = AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, m_animId).ToArtItem();
		if (pAnim)
		{
			EvaluateChannelParams evalParams;
			evalParams.m_pAnim = pAnim;
			evalParams.m_channelNameId = SID("apReference-camera1");
			evalParams.m_phase = phase;
			evalParams.m_mirror = false;
			evalParams.m_wantRawScale = true;
			if (EvaluateChannelInAnim(INVALID_SKELETON_ID, &evalParams, &jointParams))
			{			
				ndanim::JointParams alignJointParams;
				evalParams.m_channelNameId = SID("align");
				if (EvaluateChannelInAnim(INVALID_SKELETON_ID, &evalParams, &alignJointParams))
				{
					Locator transformedCameraLoc = Locator(alignJointParams.m_trans, alignJointParams.m_quat).TransformLocator(Locator(jointParams.m_trans, jointParams.m_quat));
					jointParams.m_quat = Normalize(transformedCameraLoc.GetRotation());
					jointParams.m_trans = transformedCameraLoc.GetTranslation();
				}
				return true;
			}
			else
			{
				return false;
			}
		}
		else
		{
			jointParams.m_scale = Vector(Scalar(1.0f));
			jointParams.m_quat = Quat(kIdentity);
			jointParams.m_trans = Point(kZero);
			return false;
		}
	}

public:
	StringId64 GetId()
	{
		return m_id;
	}

	StringId64 GetNameId()
	{
		return m_nameId;
	}

	bool CanAbandon()
	{
		return m_canAbandon;
	}

	StringId64 GetAnimationId()
	{
		return m_animId;
	}

	void SetId(StringId64 id)
	{
		m_id = id;
	}

	virtual bool IsAmbientShake() override { return m_isAmbientShake; }

	virtual void Init(StringId64 id, StringId64 nameId, StringId64 animId, float blend, TimeFrame fadeTime, float blendFinal, TimeFrame blendFinalTime, TimeFrame maxTimeWithoutReinit,
		float minContribute, bool isAmbientShake, float rate, bool canAbandon)
	{
		m_isAmbientShake = isAmbientShake;
		m_minContribute = minContribute;
		m_id = id;
		m_animId = animId;
		m_canAbandon = canAbandon;
		m_nameId = nameId;
		m_fadeInTime = fadeTime;
		m_startTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime();
		m_phase = 0.0f;
		m_rate = rate;
		m_fadeOutTime = fadeTime;
		m_blendFinal = blendFinal;
		m_blendFinalTime = blendFinalTime;
		m_blendingOut = false;

		const ArtItemSkeleton* pSkel = ResourceTable::LookupSkel(SID("dummy-camera-object")).ToArtItem();
		ALWAYS_ASSERT(pSkel);
		m_skelId = pSkel->m_skelId;
		m_hierarchyId = pSkel->m_hierarchyId;

		m_finalBlend = 0.0f;

		if (!IsLooping())
		{
			m_endTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime() + fadeTime + Seconds(10000.0f);
		}
		else
		{
			m_endTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime() + fadeTime + maxTimeWithoutReinit;
		}

		m_targetBlend = blend;
		m_blendSpring.Reset();

		const ArtItemAnim* pAnim = AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, m_animId).ToArtItem();
		if (pAnim == nullptr)
			MsgErr("Missing additive camera animation %s\n", DevKitOnly_StringIdToString(m_animId));
	}

	void ReInit(StringId64 nameId, TimeFrame maxTimeWithoutReinit, float blend, float rate, float blendFinal, TimeFrame blendFinalTime)
	{
		if (!IsLooping())
		{
			m_endTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime() + m_fadeOutTime + Seconds(10000.0f);
			m_phase = 0.0f;
		}
		else
		{
			m_endTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime() + m_fadeOutTime + maxTimeWithoutReinit;
		}

		//fix camera pop when reIniting camera that was blending out
		//calculate start time that gives us same blend as what we have now
		

		m_nameId = nameId;
		m_rate = rate;
		m_targetBlend = blend;
		m_blendFinal = blendFinal;
		m_blendFinalTime = blendFinalTime;
	}

	virtual void Update() override
	{
		// Advance one frame
		Advance();
	}

	virtual void SetPhase(float phase)
	{
		m_phase = phase;
	}

	virtual Locator GetLocator(const Locator& loc, float blend) override
	{				
		Locator addLoc = GetCurrentAnimationLocator();
		Locator initialLoc = GetIntialAnimationLocator();

		addLoc = initialLoc.UntransformLocator(addLoc);

		Locator newLoc = loc;
		newLoc.SetPos(loc.Pos() + Rotate(loc.Rot(), addLoc.Pos() - Point(kZero)));
		newLoc.SetRot(loc.Rot() * addLoc.Rot());

		if (g_cameraShakeDamping)
		{
			// clamp the rotation
			float dot = Dot(GetLocalZ(newLoc.GetRotation()), GetLocalZ(loc.GetRotation()));
			float angle = RADIANS_TO_DEGREES(MinMax(Acos(dot), -1.0f, 1.0f));

			float dampBlend = 1.0f;
			dampBlend = LerpScale(g_cameraShakeDampingStartAngle, g_cameraShakeDampingEndAngle, 1.0f, g_cameraShakeDampingPercent, angle);

			Quat q = Slerp(loc.GetRotation(), newLoc.GetRotation(), dampBlend);
			newLoc.SetRot(q);
		}

		float blendInFinal = m_blendFinalTime <= TimeFrameZero() ? 0.0f : ToSeconds(EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime() - m_startTime) / ToSeconds(m_blendFinalTime);
		float currTargetBlend = Lerp(m_targetBlend, m_blendFinal, MinMax01(blendInFinal));

		m_blend = m_blendSpring.Track(m_blend, currTargetBlend, GetProcessDeltaTime(), 100.0f);
		float blendOut = m_fadeOutTime <= TimeFrameZero() ? 1.0f : ToSeconds(m_endTime - EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime()) / ToSeconds(m_fadeOutTime);
		float blendIn = m_fadeInTime <= TimeFrameZero() ? 1.0f : ToSeconds(EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime() - m_startTime) / ToSeconds(m_fadeInTime);
		float currentBlend = MinMax(blendOut, 0.0f, 1.0f) * MinMax(blendIn, 0.0f, 1.0f) * m_blend * Max(m_minContribute, blend);
		if (blendOut > 1.0f)
			m_blendingOut = false;

		m_finalBlend = currentBlend;

		if (g_animOptions.m_showCameraAdditives)
		{
			MsgConPauseable("CameraAdditive: %s anim: %s  phase: %1.2f blend: %1.2f target: %1.2f BLEND OUT: %1.2f BLEND IN: %1.2f\n",
				DevKitOnly_StringIdToString(m_nameId), DevKitOnly_StringIdToString(m_animId), m_phase, m_finalBlend, m_targetBlend, blendOut, blendIn);		
		}
		Locator result = Lerp(loc, newLoc, m_finalBlend);
		result.SetRotation(Normalize(result.GetRotation()));

		return result;
	}

	virtual float GetFov(const float& fov, float blend) override
	{
		return fov;

		#if 0
		ndanim::JointParams params;
		if (GetCurrentAnimationJointParams(params))
		{
			// Conversion from Maya's focal length to field of view angle is based on assumption
			// of 35mm camera, whose film plane is 36mm wide.  So given focal length f and film
			// plane width d = 36.0f, the focal angle in radians is:
			//     angle_rad = 2 arctan(d/2f)
			// (http://en.wikipedia.org/wiki/Angle_of_view)
			float animatedFocalLength = static_cast<float>(params.m_scale.X());
			if (animatedFocalLength <= 0.0f)
				animatedFocalLength = 35.0f;

			float animatedFov = RADIANS_TO_DEGREES(2.0f * atan2f(36.0f, (2.0f * animatedFocalLength)));
			float animatedFovDefault = RADIANS_TO_DEGREES(2.0f * atan2f(36.0f, (2.0f * 35.0f)));
			float animatedFovDelta = animatedFov - animatedFovDefault;

			if (fov + animatedFovDelta > 120.0f)
				animatedFovDelta = Max(0.0f, 120.0f - fov);
			
			float blendOut = ToSeconds(m_endTime - EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime()) / ToSeconds(m_fadeOutTime);
			float blendIn = ToSeconds(EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime() - m_startTime) / ToSeconds(m_fadeInTime);
			float currentBlend = MinMax(blendOut, 0.0f, 1.0f) * MinMax(blendIn, 0.0f, 1.0f) * m_blend * Max(m_minContribute, blend);

			animatedFovDelta *= currentBlend;

			float pctDefault = static_cast<float>(params.m_scale.Y());	// 1.0f = use kDefaultFov, 0.0f = use animatedFov

			return LerpScale(0.0f, 1.0f, fov + animatedFovDelta, fov, pctDefault);
		}

		return fov;
		#endif
	}

	virtual bool IsDone() override
	{
		const ArtItemAnim* anim = AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, m_animId).ToArtItem();
		if (anim == nullptr)
		{
			MsgConScriptError("CameraShake: Missing animation '%s'\n", DevKitOnly_StringIdToString(m_animId));
			return true;
		}

		if (EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime() > m_endTime)
			return true;

		return !IsLooping() && m_phase == 1.0f;
	}

	virtual void FadeOut(TimeFrame time)
	{
		TimeFrame newEndTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime() + time;
		if (newEndTime < m_endTime)
		{
			m_fadeOutTime = time;
			m_endTime = newEndTime;
		}
		m_blendingOut = true;
	}

	virtual bool IsBlendingOut()
	{
		return m_blendingOut;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///

enum { kMaxNumShootShake = 5 };
static ShakeObject g_shootShake[kMaxCameraManagers][kMaxNumShootShake];
static int g_numShootShake[kMaxCameraManagers] = { 0 };// , 0};
static StringId64 g_shootShakeRumbleSettings = SID("shoot-shake-rumble-settings");

enum { kMaxNumNearMissShake = 5 };
static ShakeObject g_nearMissShake[kMaxCameraManagers][kMaxNumNearMissShake];
static int g_numNearMissShake[kMaxCameraManagers] = { 0 };// , 0};

enum { kMaxNumAnimationAdditiveShake = 50 };
static CameraAnimationAdditive g_animationAdditiveShake[kMaxCameraManagers][kMaxNumAnimationAdditiveShake];
static int g_numAnimationAdditiveShake[kMaxCameraManagers] = { 0 };// , 0};

static StringId64 g_explosionShakeRumbleSettings = SID("grenade-shake-rumble-settings");

static DamageMovementShake s_damageMovementShake[kMaxCameraManagers];

//----------------------------------------------------------------------------------------------------------------------
// Shake object control
//----------------------------------------------------------------------------------------------------------------------
void CameraShake::Init(int playerIndex, Point cameraPos)
{
}

#define REMOVE_FINISHED_SHAKES(_playerIndex, _shakes, _numShakes)		\
	for (int i=0; i<_numShakes[_playerIndex]; i++)					\
	{													\
		if (_shakes[_playerIndex][i].IsDone())						\
		{												\
			_shakes[_playerIndex][i] = _shakes[_playerIndex][_numShakes[_playerIndex] - 1];		\
			--_numShakes[_playerIndex];								\
			--i;										\
		}												\
	}

void CameraShake::Update(int playerIndex)
{
	// update all of our shakes
	for (int i=0; i<g_numNearMissShake[playerIndex]; i++)
	{
		g_nearMissShake[playerIndex][i].Update();
	}

	for (int i=0; i<g_numShootShake[playerIndex]; i++)
	{
		g_shootShake[playerIndex][i].Update();
	}

	for (int i=0; i<g_numAnimationAdditiveShake[playerIndex]; i++)
	{
		g_animationAdditiveShake[playerIndex][i].Update();
	}

	// remove any finished shakes
	REMOVE_FINISHED_SHAKES(playerIndex, g_nearMissShake, g_numNearMissShake );
	REMOVE_FINISHED_SHAKES(playerIndex, g_shootShake, g_numShootShake );
	REMOVE_FINISHED_SHAKES(playerIndex, g_animationAdditiveShake, g_numAnimationAdditiveShake );

	s_damageMovementShake[playerIndex].Update();
}

void CameraShake::Reset(int playerIndex)
{
	g_numShootShake[playerIndex] = 0;
	g_numNearMissShake[playerIndex] = 0;
	g_numAnimationAdditiveShake[playerIndex] = 0;
	s_damageMovementShake[playerIndex].Reset();
}

void CameraShake::DoShootShake(int playerIndex)
{
	if (EngineComponents::GetNdGameInfo()->m_isHeadless)
		return;

	DoRumble(0, g_shootShakeRumbleSettings);
	// add a new shoot shake
	/*
	if (g_numShootShake[playerIndex] < kMaxNumShootShake)
	{
		//g_shootShake[playerIndex][g_numShootShake[playerIndex]].Init(2.0f, 0.375f, Seconds(0.25f));
		DoRumble(playerIndex, g_shootShakeRumbleSettings);

		//g_numShootShake[playerIndex]++;
	}
	*/
}

void CameraShake::DoAdditiveAnimationShake(int playerIndex, StringId64 id, StringId64 nameId, StringId64 animId, float blend, TimeFrame fadeTime,
	TimeFrame maxTimeWithoutReinit, bool isAmbientShake, float minContribute, bool reinit, float rate, bool canAbandon)
{
	// renable any additive by the same name
	for (int i=0; i<g_numAnimationAdditiveShake[playerIndex]; i++)
	{
		if (g_animationAdditiveShake[playerIndex][i].GetId() == id && id != INVALID_STRING_ID_64)
		{
			if (reinit)
				g_animationAdditiveShake[playerIndex][i].ReInit(nameId, maxTimeWithoutReinit, blend, rate, 0.0f, Seconds(0.0f));
			return;
		}
	}

	// add an additive animation shake
	if (g_numAnimationAdditiveShake[playerIndex] < kMaxNumAnimationAdditiveShake)
	{
		g_animationAdditiveShake[playerIndex][g_numAnimationAdditiveShake[playerIndex]].Init(id, nameId, animId, blend, fadeTime, 0.0f, Seconds(0.0f),
			maxTimeWithoutReinit, minContribute, isAmbientShake, rate, canAbandon);
		g_numAnimationAdditiveShake[playerIndex]++;
	}
}

void CameraShake::DoAdditiveAnimationShake(int playerIndex, StringId64 id, StringId64 nameId, StringId64 animId, float blend, TimeFrame fadeTime, float blendFinal, TimeFrame blendFinalTime,
	TimeFrame maxTimeWithoutReinit, bool isAmbientShake, float minContribute, bool reinit, float rate, bool canAbandon)
{
	// renable any additive by the same name
	for (int i=0; i<g_numAnimationAdditiveShake[playerIndex]; i++)
	{
		if (g_animationAdditiveShake[playerIndex][i].GetId() == id && id != INVALID_STRING_ID_64)
		{
			if (reinit && !g_animationAdditiveShake[playerIndex][i].IsBlendingOut())
				g_animationAdditiveShake[playerIndex][i].ReInit(nameId, maxTimeWithoutReinit, blend, rate, blendFinal, blendFinalTime);
			return;
		}
	}

	// add an additive animation shake
	if (g_numAnimationAdditiveShake[playerIndex] < kMaxNumAnimationAdditiveShake)
	{
		g_animationAdditiveShake[playerIndex][g_numAnimationAdditiveShake[playerIndex]].Init(id, nameId, animId, blend, fadeTime, blendFinal, blendFinalTime,
			maxTimeWithoutReinit, minContribute, isAmbientShake, rate, canAbandon);
		g_numAnimationAdditiveShake[playerIndex]++;
	}
}

void CameraShake::CancelAdditiveAnimationShake(int playerIndex, StringId64 id, TimeFrame fadeTime)
{
	for (int i=0; i<g_numAnimationAdditiveShake[playerIndex]; i++)
	{
		if (g_animationAdditiveShake[playerIndex][i].GetId() == id)
		{
			g_animationAdditiveShake[playerIndex][i].FadeOut(fadeTime);
		}
	}
}

void CameraShake::GetArrayOfNameIds(StringId64* nameIdArray, int maxNameIds, int& nameIdArraySize)
{
	nameIdArraySize = 0;
	int playerIndex = 0;
	for (int i=0; i<g_numAnimationAdditiveShake[playerIndex]; i++)
	{
		StringId64 shakeId = g_animationAdditiveShake[playerIndex][i].GetId();
		StringId64 nameId = g_animationAdditiveShake[playerIndex][i].GetNameId();

		// only return non-zero shake ids
		if (shakeId != INVALID_STRING_ID_64)
		{
			ALWAYS_ASSERT(nameIdArraySize < maxNameIds);
			nameIdArray[nameIdArraySize] = nameId;
			nameIdArraySize++;
		}
	}
}

void CameraShake::AbandonAdditivesNotInList(int playerIndex, StringId64* activeShakeIds, int numActiveShakeIds)
{
	for (int i=0; i<g_numAnimationAdditiveShake[playerIndex]; i++)
	{
		StringId64 id = g_animationAdditiveShake[playerIndex][i].GetId();
		bool activeId = false;
		for (int j=0; j<numActiveShakeIds; j++)
		{
			if (activeShakeIds[j] == id)
			{
				activeId = true;
			}
		}

		if (!activeId)
		{
			if (g_animationAdditiveShake[playerIndex][i].CanAbandon())
			{
				g_animationAdditiveShake[playerIndex][i].SetId(INVALID_STRING_ID_64);
			}
		}
	}
}

void CameraShake::DoNearMissShake(int playerIndex)
{
	// add a new near-miss shake
	if (g_numNearMissShake[playerIndex] < kMaxNumNearMissShake)
	{
		g_nearMissShake[playerIndex][g_numNearMissShake[playerIndex]].Init(2.0f, 0.375f, Seconds(0.25f));
		g_numNearMissShake[playerIndex]++;
	}
}

void CameraShake::DoGrenadeShake(int playerIndex, float scale)
{
	// right now t1 doesn't have grenades, just molotovs, remove this
//	DoAdditiveAnimationShake(playerIndex, 0, SID("camera-grenade-large"), SID("camera-grenade-large"), scale, Seconds(0.0f), Seconds(10.0f), 1.0f);
}

void CameraShake::DoEffShake(int playerIndex, float scale, StringId64 animId, bool ambient)
{
	DoAdditiveAnimationShake(playerIndex, SID("eff"), SID("eff"), animId != INVALID_STRING_ID_64 ? animId : SID("camera-grenade-large"), scale, Seconds(0.0f), Seconds(10.0f), ambient);
}

void CameraShake::DoShake(int playerIndex, float scale, float amplitude, TimeFrame time)
{
	// add a new shoot shake
	if (g_numShootShake[playerIndex] < kMaxNumShootShake)
	{
		g_shootShake[playerIndex][g_numShootShake[playerIndex]].Init(scale, amplitude, time);
		g_numShootShake[playerIndex]++;
	}
}

void CameraShake::DoEffShake(int playerIndex, const EffectAnimInfo* pEffectAnimInfo)
{
	float scale = 1.0f;
	StringId64 animId = INVALID_STRING_ID_64;
	bool ambient = true;
	if (const EffectAnimEntryTag* pScaleTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("scale")))
	{
		scale = pScaleTag->GetValueAsF32();
	}
	if (const EffectAnimEntryTag* pAnimTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("anim")))
	{
		animId = pAnimTag->GetValueAsStringId();
	}
	if (const EffectAnimEntryTag* pAnimTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("ambient")))
	{
		ambient = (pAnimTag->GetValueAsI32() > 0);
	}
	CameraShake::DoEffShake(playerIndex, scale, animId, ambient);
}

void CameraShake::DoExplosionShake(int playerIndex, const DC::ExplosionShake* pShake, float dist)
{
	if (!pShake || dist > pShake->m_minDist)
		return;

	float shakeScale = LerpScaleClamp(pShake->m_maxDist, pShake->m_minDist, pShake->m_maxStrength, pShake->m_minStrength, dist);
	DoAdditiveAnimationShake(playerIndex, INVALID_STRING_ID_64, pShake->m_cameraAnim, pShake->m_cameraAnim, shakeScale, Seconds(0.0f), Seconds(10.0f), 1.0f);
	DoRumble(playerIndex, g_explosionShakeRumbleSettings, shakeScale);
}

void CameraShake::DoShotReactionShake(int playerIndex, float scale, float durationScale/*=1.0f*/)
{
	DoAdditiveAnimationShake(playerIndex, SID("eff"), SID("eff"), SID("camera-grenade-large"), scale, Seconds(0.0f), Seconds(10.0f * durationScale), 1.0f);
}

void CameraShake::StartFlamethrowerShake()
{
	DoAdditiveAnimationShake(0, SID("flamethrower"), SID("flamethrower"), SID("camera-shake-flamethrower"), 1.0f, Seconds(0.1f), 1.0f, Seconds(1.0f), Seconds(30.0f), false, 0.0f, true, 1.0f, false);
	
}

bool ToggleShake(DMENU::Item& item, DMENU::Message message)
{
	bool isActive = false;
	StringId64 animId = StringToStringId64(item.m_pName);
	for (int i=0; i<g_numAnimationAdditiveShake[0]; i++)
	{
		if (g_animationAdditiveShake[0][i].GetId() == animId)
		{
			isActive = true;
		}
	}

	switch (message)
	{
	case DMENU::kExecute:
		{
			if (isActive)
			{
				CameraShake::CancelAdditiveAnimationShake(0, animId, Seconds(0.5f));
			}
			else
			{
				CameraShake::DoAdditiveAnimationShake(0, animId, animId, animId, 1.0f, Seconds(0.5f), Seconds(1000.0f), true);
			}
		}
		break;
	default:
		break;
	};

	return isActive;
}

static void DoCreateShakeMenu(DMENU::Component* pComponent)
{
	AllocateJanitor jj(kAllocDebugDevMenu, FILE_LINE_FUNC);
	DMENU::Menu* pMenu = static_cast<DMENU::Menu*>(pComponent);

	extern bool g_manualCamAllowShakes;
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Show Camera Additives", DMENU::ToggleBool, &g_animOptions.m_showCameraAdditives));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Disable Camera Additives", DMENU::ToggleSettingsBool, &g_animOptions.m_disableCameraAdditives));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Allow Manual Camera Shake", DMENU::ToggleSettingsBool, &g_manualCamAllowShakes));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemDivider());

	const ArtItemSkeleton* pSkel = ResourceTable::LookupSkel(SID("dummy-camera-object")).ToArtItem();
	if (pSkel)
	{
		SkeletonId skelId = pSkel->m_skelId;

		int numAnimations = ResourceTable::GetNumAnimationsForSkelId(skelId);
		for (int i = 0; i < numAnimations; i++)
		{
			ArtItemAnimHandle anim = ResourceTable::DevKitOnly_GetAnimationForSkelByIndex(skelId, i);
			pMenu->PushBackItem(NDI_NEW DMENU::ItemBool(anim.ToArtItem()->GetName(), ToggleShake));
 		}
	}
}

void CameraShake::CreateShakeMenu(DMENU::Menu* pMenu)
{
	pMenu->SetCleanCallback(DoCreateShakeMenu);
	pMenu->MarkAsDirty();
}

void CameraShake::SetAdditiveAnimationShakePhase(int playerIndex, StringId64 id, float phase)
{
	for (int i=0; i<g_numAnimationAdditiveShake[playerIndex]; i++)
	{
		if (g_animationAdditiveShake[playerIndex][i].GetId() == id)
		{
			g_animationAdditiveShake[playerIndex][i].SetPhase(phase);
		}
	}
}

void CameraShake::ScriptSetCameraShakeStrengthMultiplier(float strength)
{
	s_scriptedBlendMultiplierFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	s_scriptedBlendMultiplier = strength;
}

float CameraShake::GetScriptedCameraShakeStrengthMultiplier()
{
	const I64 gameFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	if (gameFrame <= s_scriptedBlendMultiplierFrame + 1)
	{
		return s_scriptedBlendMultiplier;
	}
	return 1.0f;
}

float CameraShake::GetShakeBlendMultiplier()
{
	const float globalMultiplier = g_cameraOptions.m_cameraShakeStrength;
	const float scriptedMultiplier = GetScriptedCameraShakeStrengthMultiplier();
	const float result = globalMultiplier * scriptedMultiplier;
	return result;
}

Locator CameraShake::GetLocator(int playerIndex, Point cameraPos, Point targetPos, Vector upVec, float blend, bool allowAmbientShake) const
{
	Locator loc;
	loc.SetPos(cameraPos);
	loc.SetRot(QuatFromLookAt(targetPos - cameraPos, upVec));

	return GetLocator(playerIndex, loc, blend, allowAmbientShake);
}

Locator CameraShake::GetLocator(int playerIndex, Locator loc, float blend, bool allowAmbientShake) const
{
	if (g_animOptions.m_disableCameraAdditives)
	{
		return loc;
	}
	blend *= GetShakeBlendMultiplier();

	if (g_animOptions.m_showCameraAdditives)
	{
		MsgConPauseable("---------------------------------------------------\n");		
	}

	Locator initialLoc = loc;
	for (int i=0; i<g_numNearMissShake[playerIndex]; i++)
	{
		loc = g_nearMissShake[playerIndex][i].GetLocator(loc, blend);
	}

	for (int i=0; i<g_numShootShake[playerIndex]; i++)
	{
		loc = g_shootShake[playerIndex][i].GetLocator(loc, blend);
	}

	for (int i=0; i<g_numAnimationAdditiveShake[playerIndex]; i++)
	{
		if (allowAmbientShake || !g_animationAdditiveShake[playerIndex][i].IsAmbientShake())
		{
			loc = g_animationAdditiveShake[playerIndex][i].GetLocator(loc, blend);
		}
	}

	loc = s_damageMovementShake[playerIndex].GetLocator( loc, blend, allowAmbientShake);

	if (!g_cameraOptions.m_allowCameraShake)
		return initialLoc;

	return loc;
}

float CameraShake::GetFov(int playerIndex, float fov, float blend)
{
	blend *= GetShakeBlendMultiplier();
	float initialFov = fov;
	for (int i=0; i<g_numNearMissShake[playerIndex]; i++)
	{
		fov = g_nearMissShake[playerIndex][i].GetFov(fov, blend);
	}

	for (int i=0; i<g_numShootShake[playerIndex]; i++)
	{
		fov = g_shootShake[playerIndex][i].GetFov(fov, blend);
	}

	for (int i=0; i<g_numAnimationAdditiveShake[playerIndex]; i++)
	{
		fov = g_animationAdditiveShake[playerIndex][i].GetFov(fov, blend);
	}

	if (!g_cameraOptions.m_allowCameraShake)
		return initialFov;

	return fov;
}

void CameraShake::AddDamageMovement( int playerIndex, Vec2 vel, float springConst, float maxDeflection )
{
	s_damageMovementShake[playerIndex].AddDamageMovement(vel, springConst, maxDeflection);
}

SCRIPT_FUNC("enable-camera-shake-damping", DcEnableCameraShakeDamping)
{
	SCRIPT_ARG_ITERATOR(args, 3);

	float startAngle = args.NextFloat();
	float endAngle = args.NextFloat();
	float percent = args.NextFloat();

	SettingSetDefault(&g_cameraShakeDamping, false);
	SettingSetPers(SID("enable-camera-shake-damping"), &g_cameraShakeDamping, true, kPlayerModePriority, 1.0f, Seconds(0.1f));

	SettingSetDefault(&g_cameraShakeDampingStartAngle, 2.0f);
	SettingSetPers(SID("enable-camera-shake-damping"), &g_cameraShakeDampingStartAngle, startAngle, kPlayerModePriority, 1.0f, Seconds(0.1f));

	SettingSetDefault(&g_cameraShakeDampingEndAngle, 4.0f);
	SettingSetPers(SID("enable-camera-shake-damping"), &g_cameraShakeDampingStartAngle, startAngle, kPlayerModePriority, 1.0f, Seconds(0.1f));

	SettingSetDefault(&g_cameraShakeDampingPercent, 0.5f);
	SettingSetPers(SID("enable-camera-shake-damping"), &g_cameraShakeDampingPercent, startAngle, kPlayerModePriority, 1.0f, Seconds(0.1f));

	return ScriptValue(0);
}

SCRIPT_FUNC("set-camera-shake-strength-multiplier/f", DcSetCameraShakeStrengthMultiplierF)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	float multiplier = args.NextFloat();

	CameraShake::ScriptSetCameraShakeStrengthMultiplier(multiplier);

	return ScriptValue();
}

