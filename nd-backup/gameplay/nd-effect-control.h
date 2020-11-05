/*
 * Copyright (c)2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/anim/effect-anim-entry-tag.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/process/bound-frame.h"

#include "gamelib/audio/armtypes.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/render/particle/particle-handle.h"
#include "gamelib/scriptx/h/look2-defines.h"
#include "gamelib/scriptx/h/mesh-audio-config-defines.h"
#include "gamelib/audio/sfx-process.h"

/// --------------------------------------------------------------------------------------------------------------- ///
namespace DC
{
	struct MeshAudioConfig;
	struct NdProp;
}

typedef bool EffectTrackerIsDoneFunc(Process& controllingProcess, float endFrame);

/// --------------------------------------------------------------------------------------------------------------- ///
enum EOffsetFrame
{
	kWorldFrame,
	kRootFrame,
	kJointFrame,
	kAlignFrame,
	kApRefFrame
};
struct ParticleMeshAttachmentSpawnInfo;

/// --------------------------------------------------------------------------------------------------------------- ///
struct EffectSoundParams
{
	EffectSoundParams() { Reset(); }

	void Reset()
	{
		m_jointId = INVALID_STRING_ID_64;
		m_attachId = INVALID_STRING_ID_64;

		m_fGain = DbToGain(-0.0f);
		m_fPitchMod = NDI_FLT_MAX;
		m_fGainMultiplier = 1.0f;
		m_fBlendGain = 0.0f;
	}

	StringId64					m_jointId;
	StringId64					m_attachId;

	F32							m_fGain;
	F32 						m_fPitchMod;
	F32 						m_fGainMultiplier;
	F32 						m_fBlendGain;
	F32							m_waterDepth;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class EffectControlSpawnInfo : public EffectSoundParams
{
public:
	EffectControlSpawnInfo();
	EffectControlSpawnInfo(StringId64 type,
						   StringId64 name,
						   NdGameObject* pGameObject,
						   StringId64 joint,
						   bool bTracking,
						   Process* eventProcess = nullptr);
	EffectControlSpawnInfo(StringId64 type,
						   StringId64 name,
						   const BoundFrame& boundFrame,
						   bool bTracking,
						   Process* eventProcessHandle = nullptr);

	void Reset(); // clear all these damn parameters to defaults

	StringId64					m_animName;
	StringId64					m_type;				// SID("part-effect"), SID("sound-effect") or SID("foot-effect")
	StringId64					m_name;
	StringId64					m_id;				// ID that can be used to refer back to this effect
	MutableProcessHandle		m_sfxHandle;
	MutableNdGameObjectHandle	m_hGameObject;
	NdGameObjectHandle			m_hOtherGameObject;	// other object in a two-object effect

	F32							m_endFrame;
	F32							m_detachTimeout;

	bool						m_bTracking;
	bool						m_bNeedsUpdateProcess;
	bool						m_bLingeringDeath;
	bool						m_bKillWhenPhysicalized;
	bool						m_inWater;
	//bool						m_useShallowWater; // this is fun
	bool						m_inWaistDeepWater;
	bool						m_animFlipped;
	bool						m_isMeleeHit;
	bool						m_enableFiltering;

	StringId64					m_spawnerId;			// other identifier is any id used to try and disambiguate multiple concurrent effects is
	StringId64					m_rootId;
	StringId64					m_selfHitId;			// for object-on-object hits
	StringId64					m_otherHitId;			// for object-on-object hits
	StringId64					m_hitSuffix;			// for object-on-object hits
	bool						m_modBreakerFinisher;	// enable mod/breaker/finisher mode for hit-effect
	StringId64					m_bodyHitFistOverride;	// special override for the self-hit or other-hit body part id, for hit-effect
	StringId64					m_ifId;					// arbitrary "if test" callback function
	StringId64					m_ifNotId;				// arbitrary negated "if test" callback function
	StringId64					m_bodyImpactId;			// which body part to use, chest/back, etc
	float						m_rayLength;			// body/shoulder impact ray length.
	float						m_rayRadius;			// body/shoulder impact ray length.

	EOffsetFrame				m_offsetFrame;
	EOffsetFrame				m_orientFrame;
	Vector						m_offset;
	Quat						m_rot;
	Vector						m_bloodMaskOffset;
	float						m_bloodMaskAngleDeg;
	U32							m_effectFlags;

	MeshProbe::SimpleSurfaceTypeLayers m_handSurfaceType[kArmCount];
	MeshProbe::SimpleSurfaceTypeLayers m_footSurfaceType[kLegCount];
	MeshProbe::SurfaceType		m_backupFootSurfaceType[kLegCount];

	MeshProbe::SurfaceType		m_bodyImpactChestSurfaceType;
	MeshProbe::SurfaceType		m_bodyImpactBackSurfaceType;
	MeshProbe::SurfaceType		m_leftShoulderImpactSurfaceType;
	MeshProbe::SurfaceType		m_rightShoulderImpactSurfaceType;

	// If non-null, particle effect will die when this process dies.
	Process*					m_pParentProcess;
	Process*					m_eventProcess;	// process to send particle kill/collide events to
	BoundFrame					m_boundFrame;

	ParticleMeshAttachmentSpawnInfo *m_pMeshAttachmentInfo;

	// Polymorphic means of finding out if the tracker is done tracking.
	// The TrackerIsDoneFunc only used by cutscene system right now.
	// The controlling process and callback data are used by IGCs.
	Process*					m_pControllingProcess;

	U32							m_currentRopeType; //@ NDLIB problem (minor)

	const EffectAnimEntry*		m_pEffectEntry;
	uintptr_t					m_filterGroupId;

	StringId64				m_animLayerId = INVALID_STRING_ID_64;
	AnimInstance::ID		m_animInstanceId = INVALID_ANIM_INSTANCE_ID;

	int m_saveType = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class IEffectControl
{
public:
	static void Init();

	virtual void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound) = 0;
	virtual void RelocateMeshAudioConfig(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound) = 0;

	virtual void DebugDraw(const NdGameObject* pOwner) const = 0;

	virtual void AddMeshAudioConfig(const DC::MeshAudioConfig* pMeshAudioConfig, DC::Look2BodyPart part = DC::kLook2BodyPartLegs) = 0;
	virtual void RemoveMeshAudioConfigByBodyPart(DC::Look2BodyPart part, DC::MeshAudioOptions options) = 0;
	virtual void ResetMeshAudioConfig() = 0;
	virtual void OnPropAdded(const NdGameObject* pPropObject, const DC::NdProp* pProp)   = 0;
	virtual void OnPropRemoved(const NdGameObject* pPropObject, const DC::NdProp* pProp) = 0;

	virtual void PopulateSpawnInfo(EffectControlSpawnInfo& info,
								   const EffectAnimInfo* pEffectAnimInfo,
								   NdGameObject* pGameObject,
								   Process* pControllingProcess = nullptr,
								   bool inWater = false,
								   bool inWaistDeepWater = false) = 0;

	virtual MutableProcessHandle FireEffect(EffectControlSpawnInfo* pInfo) = 0;

	virtual void EnableEffectFiltering(float sfxLimitingTime = -1.0f) = 0;
	virtual void DisableEffectFiltering() = 0;

	virtual ParticleHandle GetLastParticleHandle() const = 0;
	virtual const char* GetMeleeSuffix(DC::Look2BodyPart part, U8 loc) const = 0;
	virtual const char* GetCharacterPrefix() const = 0;
	virtual const char* GetPartPrefix(DC::Look2BodyPart part) const = 0;
	virtual const char* GetGearEffectSuffixLower() const = 0;
	virtual const char* GetGearEffectSuffixUpper() const = 0;

	virtual void SetPlayer(bool isPlayer) = 0;

	virtual bool IsEffectFilteredByCriterion(const EffectControlSpawnInfo* pInfo) const { return false; }

	virtual StringId64 GetSoundForMeshEffect(const NdGameObject* pGo,
									 StringId64 meshEffType,
									 StringId64 meshEffSuffix,
									 char* debugSoundName	 = nullptr,
									 size_t soundNameBufSize = 0) const = 0;

	const EffectAnimInfo* HandleScriptEffect(const NdGameObject* pGo,
											 const NdGameObject* pOtherGo,
											 const EffectAnimInfo* pEffectAnimInfo,
											 StringId64 scriptId) const;

	///---------------------------------------------------------------------------///
	/// Fire single sound with surface-blend
	///---------------------------------------------------------------------------///
	static MutableProcessHandle FireSurfaceSound(StringId64 soundNameId,
											     float surfaceBlend,
											     NdGameObject* pGameObject,
											     Point_arg position,
											     const EffectSoundParams& params,
											     SfxProcess::SpawnFlags spawnFlags);

	static MutableProcessHandle FireAdditiveSound(StringId64 soundNameId,
												  float additivePercentage,
												  NdGameObject* pGameObject,
												  Point_arg position,
												  const EffectSoundParams& params,
												  SfxProcess::SpawnFlags spawnFlags,
												  bool hackUseSurfaceBlend);

	///---------------------------------------------------------------------------///
	/// convert MeshProbe::SurfaceType to SurfaceSound (Id)
	///---------------------------------------------------------------------------///
	struct SurfaceSound
	{
		SurfaceSound() { Reset(); }

		SurfaceSound(StringId64 surfaceId, StringId64 soundId)
			: m_primarySurfaceId(surfaceId)
			, m_primarySoundId(soundId)
			, m_secondarySurfaceId(INVALID_STRING_ID_64)
			, m_secondarySoundId(INVALID_STRING_ID_64)
		{
			m_blend = (m_primarySoundId != INVALID_STRING_ID_64) ? 1.f : 0.f;
			m_primaryPercentage = (m_primarySoundId != INVALID_STRING_ID_64) ? 1.f : 0.f;
			m_secondaryPercentage = 0.f;
		}

		void Reset()
		{
			ClearBase();
			ClearAdditive();
		}

		void ClearBase()
		{
			m_primarySurfaceId = INVALID_STRING_ID_64;
			m_secondarySurfaceId = INVALID_STRING_ID_64;

			m_primarySoundId = INVALID_STRING_ID_64;
			m_secondarySoundId = INVALID_STRING_ID_64;

			m_blend = 0.f;
			m_primaryPercentage = 0.f;
			m_secondaryPercentage = 0.f;
		}

		void ClearAdditive()
		{
			m_additiveSurfaceId = INVALID_STRING_ID_64;
			m_additiveSoundId = INVALID_STRING_ID_64;
			m_additivePercentage = 0.f;
		}

		StringId64	m_primarySurfaceId;
		StringId64	m_secondarySurfaceId;

		StringId64	m_primarySoundId;
		StringId64	m_secondarySoundId;

		float		m_blend;
		float		m_primaryPercentage = 0.f;
		float		m_secondaryPercentage = 0.f;

		// additive surface sound for water, decal(glass), etc
		StringId64	m_additiveSurfaceId = INVALID_STRING_ID_64;
		StringId64	m_additiveSoundId = INVALID_STRING_ID_64;
		float		m_additivePercentage = 0.f;

	};

	///---------------------------------------------------------------------------///
	/// fire surface-sound, which could be single sound or blended sound.
	///---------------------------------------------------------------------------///
	static MutableProcessHandle FireSurfaceSound(const SurfaceSound& surfaceSound,
											     NdGameObject* pGameObject,
											     Point_arg position,
											     const EffectSoundParams* pParams = nullptr,
											     SfxProcess::SpawnFlags spawnFlags = 0U);

	static StringId64 ExtractBaseLayerSurfaceType(const MeshProbe::SurfaceType& info);
	static StringId64 ExtractBaseLayerSurfaceType(const MeshProbe::SurfaceTypeLayers& info);
};

/// --------------------------------------------------------------------------------------------------------------- ///
inline StringId64 GetEffTagAsSID(const EffectAnimEntry* pEffect, StringId64 tagId, StringId64 defaultVal)
{
	const EffectAnimEntryTag* pTag = pEffect->GetTagByName(tagId);
	return pTag ? pTag->GetValueAsStringId() : defaultVal;
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline StringId64 GetEffTagAsSID(const EffectAnimEntry* pEffect,
								 StringId64 tagId1,
								 StringId64 tagId2,
								 StringId64 defaultVal)
{
	return GetEffTagAsSID(pEffect, tagId1, GetEffTagAsSID(pEffect, tagId2, defaultVal));
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline StringId64 GetEffTagAsSID(const EffectAnimEntry* pEffect,
								 StringId64 tagId1,
								 StringId64 tagId2,
								 StringId64 tagId3,
								 StringId64 defaultVal)
{
	return GetEffTagAsSID(pEffect, tagId1, GetEffTagAsSID(pEffect, tagId2, GetEffTagAsSID(pEffect, tagId3, defaultVal)));
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline F32 GetEffTagAsF32(const EffectAnimEntry* pEffect, StringId64 tagId, F32 defaultVal)
{
	const EffectAnimEntryTag* pTag = pEffect->GetTagByName(tagId);
	return pTag ? pTag->GetValueAsF32() : defaultVal;
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline U32 GetEffTagAsU32(const EffectAnimEntry* pEffect, StringId64 tagId, U32 defaultVal)
{
	const EffectAnimEntryTag* pTag = pEffect->GetTagByName(tagId);
	return pTag ? pTag->GetValueAsU32() : defaultVal;
}
