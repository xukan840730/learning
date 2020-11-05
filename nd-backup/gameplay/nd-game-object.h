/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/math/sphere.h"

#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-instance.h"
#include "ndlib/anim/anim-plugin-context.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/attach-system.h"
#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/fx/fx-handle.h"
#include "ndlib/process/bound-frame.h"
#include "ndlib/process/process.h"
#include "ndlib/render/highlights-mgr.h"
#include "ndlib/render/ndgi/ndgi.h"
#include "ndlib/render/ngen/binary-geo-format.h"
#include "ndlib/render/ngen/meshraycaster.h"
#include "ndlib/render/interface/fg-instance.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/particle-defs.h"

#include "gamelib/audio/armtypes.h"
#include "gamelib/gameplay/faction-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/simple-grid-hash.h"
#include "gamelib/gameplay/nd-locatable.h"
#include "gamelib/gameplay/nd-attachment-ctrl.h"
#include "gamelib/ndphys/collision-filter.h"
#include "gamelib/scriptx/h/nd-script-func-defines.h"
#include "gamelib/spline/catmull-rom.h"
#include "gamelib/state-script/ss-action.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimControl;
class ArtItemAnim;
class ArtItemCollision;
class ArtItemGeo;
class ArtItemSkeleton;
class BgAttacher;
class BitStream;
class BlinkController;
class CompositeBody;
class DynamicNavBlocker;
class EffectAnimInfo;
class EffectControlSpawnInfo;
class EffectList;
class FactDictionary;
class GesturePermit;
class GroundHugController;
class IDrawControl;
class IEffectControl;
class IGestureController;
class ILimbManager;
class IVehicle;
class InstanceTextureTable;
class JointLimits;
class JointModifiers;
class LoadedTexture;
class NdAttackHandler;
class NdFoliageSfxControllerBase;
class NdInteractControl;
class NdNetController;
class NdNetLodController;
class NdNetPlayerTracker;
class NdSubsystemMgr;
class NdSubsystemAnimController;
class NdVoxControllerBase;
class NetPlayerTracker; // to NDLIB this is an opaque data type -- the game must define it
class Pat;
class ParticleHandle;
class PlatformControl;
class ProcessSpawnInfo;
class RegionControl;
class ResolvedLook;
class RigidBody;
class RigidBody;
class SfxProcess;
class SfxSpawnInfo;
class SpawnInfo;
class SplasherController;
class SplasherSet;
class SplasherSkeletonInfo;
class SplineTracker;
class SsAnimateController;
class SsInstance;
class StaticNavBlocker;
class Targetable;
class VbHandle;
class MeshRayCastJob;
class TextPrinter;
class IApBlocker;
class EmotionControl;
class Character;
class AttachmentCtrl;

struct AnkleInfo;
struct WristInfo;
struct CompositeBodyInitInfo;
struct EvaluateJointOverridePluginData;
struct JointOverrideData;
struct LookDebugInfo;
struct ProbeTreeWeightControl;
struct ReceiverDamageInfo; // to NDLIB this is an opaque data type -- the game must define it
struct ResolvedBoundingData;
struct ResolvedTintPalette;
struct SpecularIblProbeParameters;
struct SsAnimateParams;

namespace DC
{
	struct JointShaderDriverList;
	struct NdSsOptions;
	struct PhysFxSet;
	struct ShimmerSettings;
	struct AnimState;
	struct CharacterCollision;
	struct CharacterCollisionT1;
	struct Map;
	struct CustomAttachPointSpecArray;

	typedef I32 SplasherLimb;
}

namespace ndjob
{
	class Counter;
}

struct hknpBodyId;

PGO_BOOL_DECL(g_bJobfyFindNdGameObjects, true);

/// --------------------------------------------------------------------------------------------------------------- ///
struct NdGameObjectConfig
{
	U32 m_animControlMaxLayers;
	U32 m_drawControlMaxMeshes;

	NdGameObjectConfig() : m_animControlMaxLayers(4), m_drawControlMaxMeshes(1) { }
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NdGameObject : public NdLocatableObject
{
private:
	typedef NdLocatableObject ParentClass;

public:
	FROM_PROCESS_DECLARE(NdGameObject);

	union GoFlags
	{
		U64			m_rawBits;
		struct
		{
			U64				m_inited								: 1;	// Has the data in the focusable ever been updated?
			U64				m_invisibleToNpcs						: 1;	// disable npc's vision only
			U64				m_inaudibleToNpcs						: 1;	// disable npc's hearing only
			U64				m_unsmellableToNpcs						: 1;	// disable npc's smell only
			U64				m_pointOfInterest						: 1;	// Point of interest (for NPCs)
			U64				m_customPoints							: 1;	// Points are provided and not retrieved from the attach system
			U64				m_bTurnReticleRed						: 1;	// If set then the reticle should turn red when we are over this object
			U64				m_bDisableCanHangFrom					: 1;
			U64				m_useLargeEffectList					: 1;
			U64				m_needsOverlayInstanceTexture			: 1;
			U64				m_needsWetmaskInstanceTexture			: 1;
			U64				m_needsMudmaskInstanceTexture			: 1;
			U64				m_needsDamagemaskInstanceTexture		: 1;
			U64				m_needsGoremaskInstanceTexture			: 1;
			U64				m_needsDynamicGoremaskInstanceTexture	: 1;
			U64				m_needsWoundmaskInstanceTexture			: 1;
			U64				m_needsBurnMaskInstanceTexture			: 1;
			U64				m_needsSnowMaskInstanceTexture			: 1;
			U64				m_needsSpecularCubemapTexture			: 1;
			U64				m_disableInstanceTextures				: 1;
			U64				m_autoUpdateRegionControl				: 1;
			U64				m_bIsPlatform							: 1;
			U64				m_disableEffects						: 1;
			U64				m_disableNetUpdates						: 1;
			U64				m_replyAimingAtFriend					: 1;
			U64				m_usePlayersWetMask						: 1;
			U64				m_updatesDisabled						: 1;
			U64				m_instanceAoEnabled						: 1;
			U64				m_instanceAoCurrent						: 1;
			U64				m_instanceAoDesired						: 1;
			mutable U64		m_shaderInstParamsErrPrinted			: 1;
			U64				m_hasHiddenJoints						: 1;
			U64				m_isVisibleInListenMode					: 1;
			U64				m_enableCameraParenting					: 1;
			U64				m_gasMaskMesh							: 1;
			U64				m_playerCanTarget						: 1;
			U64				m_disablePlayerParenting				: 1;
			U64				m_forceAllowNpcBinding					: 1;
			U64				m_listenerSfxBias						: 1;	// Use listener bias sfx logic if this is set.
			U64				m_isPoiLookAt							: 1;
			U64				m_updateSpecCubemapEveryFrame			: 1;
			U64				m_disableFgVis							: 1;
			U64				m_autoTannoy							: 1;
			U64				m_disableAnimationAfterUpdate			: 1;
			U64				m_hasBackpack							: 1;
			U64				m_neverBreakArrow						: 1; // Arrows striking this object never break.
			U64				m_registeredInteractable				: 1;
			U64				m_managesOwnHighContrastMode			: 1;
			U64				m_mouthUnderwater						: 1;
			U64				m_waterDeath							: 1;
			U64				m_unused								: 11;
		};
	};

	NdGameObjectConfig m_gameObjectConfig;

	NdGameObject();
	virtual ~NdGameObject() override;


	///-----------------------------------------------------------------------------------------///
	// generic MeshRaycast result for sound
	///-----------------------------------------------------------------------------------------///
	enum MeshRaycastType
	{
		kMeshRaycastGround = 0,
		kMeshRaycastWater,
		kMeshRaycastCount,
	};

	struct ALIGNED(16) MeshRaycastResult
	{
		MeshRaycastResult()
		{
			Reset(SID("stone-asphalt"));
		}

		void Reset(StringId64 initSurfaceType)
		{
			m_valid = false;
			m_contactFrameNumber = -1;
			m_surfaceFrameNumber = -1;

			m_contact = BoundFrame(kIdentity);

			m_surfaceTypeInfo.Clear();
			m_surfaceTypeInfo = MeshProbe::SurfaceType(initSurfaceType);

			m_positionWs = kOrigin;
			m_normalWs = kZero;
			m_vertexWs0 = kOrigin;
			m_vertexWs1 = kOrigin;
			m_vertexWs2 = kOrigin;

			m_rayStart = kOrigin;
			m_rayEnd = kOrigin;
			m_t = -1.f;
			m_radius = 0.f;
		}

		//+ the following is for debug.
		Point	m_positionWs;
		Vector	m_normalWs;
		Point	m_vertexWs0;
		Point	m_vertexWs1;
		Point	m_vertexWs2;
		Point	m_rayStart;
		Point	m_rayEnd;
		float	m_t;
		float	m_radius;
		//- the following is for debug.

		bool					m_valid;
		BoundFrame				m_contact;
		I64						m_contactFrameNumber;
		I64						m_surfaceFrameNumber;

		StringId64				m_backupBaseLayerSt = INVALID_STRING_ID_64; // used by EFF sound in case sound probing doesn't have any base layer surface type.

		MeshProbe::SurfaceTypeLayers m_surfaceTypeInfo;
	};

	// used for limb surface type.
	struct ALIGNED(16) MeshRaycastContext
	{
		MeshRaycastContext()
		{
			memset(this, 0, sizeof(MeshRaycastContext));
		}

		MeshRaycastType			m_type;
		MutableNdGameObjectHandle		m_hOwner;
		I32						m_index;
		bool					m_debugDrawVoidSurface;
	};

	static void SetMeshRayResult(const MeshProbe::CallbackObject* pObject,
								 const MeshProbe::Probe* pRequest,
								 const Binding& binding,
								 MeshRaycastResult* pResult);

	static void OnSurfaceTypeReady(const MeshProbe::FullCallbackObject* pObject,
		MeshRaycastResult* pResult,
		const MeshProbe::Probe* pRequest);

	static void DebugDrawMeshRaycastResult(const MeshRaycastResult* pResult);

	struct ALIGNED(16) WheelMeshRaycastResult
	{
		void Reset()
		{
			m_valid = false;
			m_collisionValid = false;
			m_frameNumber = 0;
			m_mrcSurfaceValid = false;
			m_mrcSurfaceFrameNumber = 0;
			m_mrcSurfaceLastValidFrameNumber = 0;
			m_mrcSurfaceInfo.Clear();
			m_delta = 0.0f;
		}

		bool							m_valid;
		I64								m_frameNumber;
		Point							m_mrcContactPoint;
		Vector							m_mrcContactNormal;
		bool							m_mrcSurfaceValid;
		I64								m_mrcSurfaceFrameNumber;
		I64								m_mrcSurfaceLastValidFrameNumber;
		MeshProbe::SurfaceTypeResult	m_mrcSurfaceInfo;
		Color							m_mrcVertexColor0;
		Color							m_mrcVertexColor1;
		bool							m_collisionValid;
		Point							m_collisionPoint;
		Vector							m_collisionNormal;
		Pat								m_collisionPat;
		float							m_delta;
		SpringTracker<float>			m_deltaSpringTracker;
	};

	struct WheelMeshRaycastContext
	{
		MutableNdGameObjectHandle	m_hOwner;
		I64					m_frameNumber;
		I32					m_wheelIndex;
	};

	virtual bool PlayerCanTarget() const { return m_goFlags.m_playerCanTarget; }
	void SetIgnoredByNpcs(bool ignore);
	void SetIgnoredByNpcsForOneFrame();
	bool IgnoredByNpcs() const;
	virtual bool NeverBreakArrow() const { return m_goFlags.m_neverBreakArrow; }
	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual Err PostInit(const ProcessSpawnInfo& spawn) override;
	virtual void GoToInitialScriptState();
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void OnError() override;
	virtual void OnKillProcess() override;
	virtual void DetachFromLevel() override;
	virtual bool RespondsToScriptUpdate() const override;
	virtual void DispatchEvent(Event& e, bool scriptDispatch) override;
	virtual bool RespondsToScriptEvent(StringId64 messageId) const override;
	virtual void EventHandler(Event& event) override;
	virtual void ProcessDispatchError(const char* strMsgIn) override;
	virtual int  GetChildScriptEventCapacity() const { return 0; }
	void RegisterChildScriptEvent(StringId64 eventId);
	virtual void PreUpdate() override;
	virtual void BatchDestroyCompositeBodyPass1(hknpBodyId* pEntityList, I32& destroyCount, I32 maxDestroyCount);
	virtual void BatchDestroyCompositeBodyPass2();
	virtual void ResolveLook(ResolvedLook& resolvedLook, const SpawnInfo& spawn, StringId64 desiredActorOrLookId);
	virtual void CollectQueries() {}

	virtual void SetScale(Vector_arg scale);
	virtual Vector GetScale() const;
	virtual void SetUniformScale(float scale);
	virtual float GetUniformScale() const;

	virtual bool MeetsCriteriaToDebugRecord(bool& outRecordAsPlayer) const; // Should JAF Anim recorder record this process? (outRecordAsPlayer is false when passed in)
	virtual bool ShouldStopDebugRecording() const { return false; }

	const NdInteractControl* GetInteractControl() const { return m_pInteractCtrl; }
	NdInteractControl* GetInteractControl() { return m_pInteractCtrl; }

	const AttachmentCtrl* GetAttachmentCtrl() const { return m_pAttachmentCtrl; }
	AttachmentCtrl* GetAttachmentCtrl() { return m_pAttachmentCtrl; }

	TimeFrame GetSpawnTime() const { return m_spawnTime; }

	void RegisterWithInteractablesMgr();

	virtual bool UpdateFacts() { return true; }
	FactDictionary* UpdateAndGetFactDict()
	{
		UpdateFacts();
		return GetFactDict();
	}

	virtual bool IsDialogAllowed() const { return true; }
	virtual bool IsMouthUnderwater() const { return m_goFlags.m_mouthUnderwater; }
	virtual bool IsWaterDeath() const { return m_goFlags.m_waterDeath; }
	virtual bool HasHead() const { return true; }
	virtual bool IsPlayer() const { return false; }
	virtual DC::AnimLod GetAnimLod() const { return m_pAnimData->m_animLod; }
	virtual void SetAnimLod(DC::AnimLod animLod) { m_pAnimData->m_animLod = animLod; }

	virtual bool CameraStrafeEnableNpcCollision() const { return false; }

	virtual IVehicle* GetVehicleInterface() { return nullptr; }
	virtual const IVehicle* GetVehicleInterface() const { return nullptr; }

	virtual void OnAttach() {};
	virtual void OnDetach() {};

	virtual void AssociateWithLevel(const Level* pLevel) override;

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Updating
	/// --------------------------------------------------------------------------------------------------------------- ///
	STATE_DECLARE_OVERRIDE(Active);

	virtual void ProcessScriptUpdate() override;
	virtual void FinishUpdate();

	virtual bool DisableAnimation();
	virtual void DisableAnimationAfterUpdate() { m_goFlags.m_disableAnimationAfterUpdate = true; }
	virtual void EnableAnimation();
	bool IsAnimationEnabled() const { return m_pAnimData->m_animConfig != FgAnimData::kAnimConfigNoAnimation; }

protected:
	// Calculates m_velocityWs, m_velocityPs, m_angularVecWs, m_prevPosWs, m_prevPosPs, and m_prevCheckPointPosWs
	virtual void UpdateVelocityInternal();

	virtual Err CreateInteractControl(const SpawnInfo& spawn) { return Err::kOK; }

public:
	virtual void DisableUpdates();
	virtual void EnableUpdates();
	virtual void FastProcessUpdate(F32 dt) {};

	void UpdateBSphereWs(FgAnimData* pAnimData, Locator align2world);
	void AnimDataRecalcBoundAndSkinning(FgAnimData* pAnimData);
	void FastAnimDataUpdateBoundAndSkinning(FgAnimData* pAnimData);
	static void FastAnimDataUpdate(FgAnimData* pAnimData, F32 dt);
	static void FastAnimDataUpdateInner(FgAnimData* pAnimData, F32 dt, bool skipRigidBodySyncFromGame = false);
	static void PropagateSkinningBoneMats(FgAnimData* pAnimData);

	virtual bool ShouldUpdateSplashers() const { return true; }
	virtual bool ShouldUpdateWetness() const { return false; } // this is an "opt-in" feature that child classes should request through override
	virtual void UpdateWetness();
	virtual void AcquireWetMasks();

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Site interface
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual Locator GetSite(StringId64 nameId) const;

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Mesh
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual StringId64 GetLookId() const { return INVALID_STRING_ID_64; }
	virtual StringId64 GetBaseLookId() const { return INVALID_STRING_ID_64; }
	StringId64 GetResolvedLookId() const { return m_resolvedLookId; }
	virtual StringId64 GetUserIdUsedForLook() const { return m_userIdUsedForLook; }
	U32 GetLookCollectionRandomizer() const { return m_lookCollectionRandomizer; }
	U32 GetLookTintRandomizer() const		{ return m_lookTintRandomizer; }
	virtual StringId64 GetSkelNameId() const;
	virtual StringId64 GetDefaultAmbientOccludersId() const { return INVALID_STRING_ID_64; }
	virtual StringId64 GetRenderTargetSamplingId() const { return INVALID_STRING_ID_64; }
	virtual void GetAssociatedProcessIdsForRT(U32* outIds, U32& outnum, const U32 maxNum) const { outnum = 0; }
	virtual U32F GetMaxNumDrivenInstances() const { return 64; }
	U32F GetAmbientOccludersObjectId() const { return GetProcessId(); }
	void ForceLodIndex(I32 lodIndex);
	virtual bool HasBackpack() const { return false; }

	// shader instance parameters
	virtual void AllocateShaderInstanceParams();
	void AllocateShaderInstanceParams(int meshIndex);
	bool HasShaderInstanceParams() const;
	bool HasShaderInstanceParams(int meshIndex) const;
	F32 GetShaderInstanceParam(int vecIndex, int componentIndex = 0) const;
	bool GetShaderInstanceParam(Vec4* pOut, int vecIndex) const;
	F32 GetMeshShaderInstanceParam(int meshIndex, int vecIndex, int componentIndex = 0) const;
	bool GetMeshShaderInstanceParam(Vec4* pOut, int meshIndex, int vecIndex) const;

	bool SetShaderInstanceParam(int vecIndex, F32 value, int componentIndex = 0);
	bool SetShaderInstanceParam(int vecIndex, Vector_arg value);
	bool SetShaderInstanceParam(int vecIndex, Vec4_arg value);
	bool SetMeshShaderInstanceParam(int meshIndex, int vecIndex, F32 value, int componentIndex = 0);
	bool SetMeshShaderInstanceParam(int meshIndex, int vecIndex, Vector_arg value);
	bool SetMeshShaderInstanceParam(int meshIndex, int vecIndex, Vec4_arg value);
	void SetSpawnShaderInstanceParams(const ProcessSpawnInfo& spawn);
	bool SetShaderInstanceParamsFromPalette(ResolvedTintPalette& resolvedPalette);
	bool SetMeshShaderInstanceParamsFromPalette(int meshIndex, ResolvedTintPalette& resolvedPalette);

	virtual void NotifyMeshUnloaded(const ArtItemGeo* artItem) {}
	void ResolveInstanceMaterialRemaps(ResolvedLook& resolvedLook, const SpawnInfo& spawn);

	void ShowMeshByMaterial(const char* pMatName, bool show);
	bool CheckHasMaterial(const char* pMatName) const; // just checks lod 0

	void UpdateAssociatedBackground();

	void ChangeMaterial(const char* from, const char* to, StringId64 materialMeshId = INVALID_STRING_ID_64, bool fixupOnSwap = false);
	void ChangeMaterial(StringId64 from, StringId64 to, StringId64 materialMeshId = INVALID_STRING_ID_64, bool fixupOnSwap = false);
	void ApplyMaterialSwapMap(const DC::Map* pMaterialSwapMap);
	void ResetAllMaterials();
	void ApplyMaterialRemap();
	void RemoveMaterialRemap();

	virtual void SetAmbientScale(float scale);

	bool AddTextureDecal(const Vec2_arg offset, const Vec2_arg scale);
	F32 GetUvDistToTextureDecal(const Vec2_arg hitUv, const Vec2_arg invUvSpaceScale);

	virtual void Highlight(const Color& color, HighlightStyle style = kHighlightStyleGlowNormal); // call every frame to highlight the object
	virtual void Highlight(const Color& color, const Vec4& params0, const Vec4& params1, HighlightStyle style = kHighlightStyleGlowNormal); // call every frame to highlight the object

	void SetShimmer(StringId64 shimmerSettingsName, bool allowAllocations = false);
	void SetShimmerIntensity(F32 val);
	void FadeShimmerIntensity(F32 targetVal, F32 blendTime);
	void UpdateShimmer();
	bool ShouldUpdateShimmer() const { return m_updateShimmer && (m_shimmerIntensity > 0.0f); };

	void SetInstanceAoDesired(bool isDesired) { m_goFlags.m_instanceAoDesired = isDesired; }

	void SetHiresAnimGeo(bool wantHiresAnimGeo); // enable or disable the special LOD-chain/rigging that is used for hi-res animations (i.e. cinematics)
	bool IsHiresAnimGeoShown() const;

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Listen Mode
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool AllowListenModeReveal(F32& intensityFactor, bool allowSkillSuppression = true) const { intensityFactor = 1.0f;  return true; } //stay registered, but temporarily disable most listen mode reveals
	virtual void MakeListenModeNoise(bool force = false) {}
	virtual void MakeLoudListenModeNoise(bool force = false) {}
	virtual F32 GetListenModeMaxIntensity() const { return 1.0f; }
	virtual F32 GetListenModeIntensityMultiplier() const { return 1.0f; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Animation
	/// --------------------------------------------------------------------------------------------------------------- ///
	SkeletonId GetSkeletonId() const { return m_skeletonId; }
	IDrawControl* GetDrawControl() const;
	AnimControl* GetAnimControl() const; //<! ptr to anim control or NULL
	void ChangeAnimConfig(FgAnimData::AnimConfig config);
	const Sphere GetBoundingSphere() const;
	const SMath::Mat34* GetInvBindPoses() const;
	FgAnimData* GetAnimData();
	const FgAnimData* GetAnimData() const;
	virtual const char* GetOverlayBaseName() const { return nullptr; }
	virtual U32F CollectEndEffectorsForPluginJointSet(StringId64* pJointIdsOut, U32F maxJointIds) const { return 0; }

	virtual StringId64 GetDefaultStateBlendOverlayId() const { return INVALID_STRING_ID_64; }
	virtual StringId64 GetDefaultAnimBlendOverlayId() const { return INVALID_STRING_ID_64; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	/// for pickup and inventory system.
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual StringId64 GetPickupItemId() const { return INVALID_STRING_ID_64; }
	virtual I32 GetPickupItemAmount() const { return 0; }
	virtual I32 GetPickupAmmoAmount() const { return 0; }
	virtual bool PickupIgnoreVisCheck() const { return false; }
	virtual StringId64 GetWeaponPartPickupAnim() const {return INVALID_STRING_ID_64;}
	virtual bool WearingGasMask() const { return false; }


	virtual SsAnimateController* GetPrimarySsAnimateController() const { return nullptr; }
	virtual SsAnimateController* GetCinematicSsAnimateController() const { return GetPrimarySsAnimateController(); } // derived classes may redefine

	virtual IGestureController* GetGestureController() const { return nullptr; }
	virtual GesturePermit* CreateGesturePermit() { return nullptr; }
	virtual float GetGestureBlendFactorSpring() const { return -1.0f; }
	virtual Point GetAimOriginPs() const { return GetTranslationPs(); }
	virtual Point GetLookOriginPs() const { return GetTranslationPs(); }
	Point GetGestureOriginOs(StringId64 gestureId, StringId64 typeId) const;

	virtual BlinkController* GetBlinkController() { return nullptr; }
	virtual const BlinkController* GetBlinkController() const { return nullptr; }

	virtual ILimbManager* GetLimbManager() { return nullptr; }

	virtual void SetLightTable(LightTable* pLightTable) {}
	virtual void SetTweakedLightTable(LightTable* pTweakedLightTable) {}
	virtual void SetAnimatedLights(const AnimatedLight* pAnimatedLights) {}

	virtual bool IsUsingApOrigin() const;
	virtual bool GetApOrigin(Locator& apRef) const; // Locator version is deprecated
	virtual bool GetApOrigin(BoundFrame& apRef) const;

	IEffectControl* GetEffectControl() { return m_pEffectControl; }				//<! ptr to effect control or NULL
	const IEffectControl* GetEffectControl() const { return m_pEffectControl; }	//<! ptr to effect control or NULL
	virtual void HandleTriggeredEffect(const EffectAnimInfo* pEffectAnimInfo);
	virtual void PopulateEffectControlSpawnInfo(const EffectAnimInfo* pEffectAnimInfo, EffectControlSpawnInfo& effInfo);
	virtual const NdGameObject* GetOtherEffectObject(const EffectAnimInfo* pEffectAnimInfo) { return nullptr; }
	void TriggeredEffects(const EffectList* pEffectList);
	virtual void HandleTriggeredCharacterEffect(const EffectAnimInfo* pEffectAnimInfo, Character* pChar) {} // in case owning character wants to forward us his effs
	virtual void SetTrackedRenderTargetParticleHandle(const ParticleHandle *pHPart, const DC::PartBloodSaveType type) { }
	virtual bool AllowRenderTargetEffects() const { return true; }
	virtual bool AllowAttachedEffects() const { return true; } // whether to allow effects attached to this object. useful for killing all decals attached to game object


	virtual	JointModifiers*			GetJointModifiers()			{ return nullptr; }
	virtual const JointModifiers*	GetJointModifiers() const	{ return nullptr; }

	virtual void SetJointLimits(JointLimits* pJointLimits) { GetAnimData()->m_pJointLimits = pJointLimits; }
	virtual JointLimits* GetJointLimits()				{ return GetAnimData() ? GetAnimData()->m_pJointLimits : nullptr; }
	virtual const JointLimits* GetJointLimits() const	{ return GetAnimData() ? GetAnimData()->m_pJointLimits : nullptr; }

	StringId64 GetCurrentAnimState() const;
	StringId64 GetCurrentAnim(bool usePhaseAnim = true) const;
	bool IsAnimPlaying(StringId64 animName, StringId64 layerName = INVALID_STRING_ID_64, F32* pOutPhase = nullptr, F32* pOutFrame = nullptr) const;
	bool IsCurrentAnim(StringId64 animName, StringId64 layerName, F32* pOutPhase = nullptr, F32* pOutFrame = nullptr) const;
	virtual bool IsSimpleAnimating() const { return false; };

	static const I32 kMaxHiddenJoints = 16;
	virtual I32 GetHiddenJointIndices(I32* pJointIndices) { return 0; }

	void TestIsInBindPose() const;
	virtual bool MayBeInBindPose() const { return false; } // for bind-pose optimization

	DC::AnimateExit GetDefaultAnimateExitMode() const { return DC::kAnimateExitIdle; }

	virtual NdSubsystemMgr* GetSubsystemMgr() { return nullptr; }
	virtual const NdSubsystemMgr* GetSubsystemMgr() const { return nullptr; }

	virtual StringId64 GetDefaultSubsystemControllerType() const {return INVALID_STRING_ID_64;}
	virtual NdSubsystemAnimController* GetActiveSubsystemController(StringId64 type=INVALID_STRING_ID_64) { return nullptr; }
	virtual const NdSubsystemAnimController* GetActiveSubsystemController(StringId64 type=INVALID_STRING_ID_64) const { return nullptr; }

	void SetDefaultInstanceCallbacks(AnimStateLayer* pLayer);
	void AnimStateInstancePrepare(StringId64 layerId,
								  StateChangeRequest::ID requestId,
								  AnimStateInstance::ID instId,
								  bool isTop,
								  const DC::AnimState* pAnimState,
								  FadeToStateParams* pParams);
	virtual void AnimStateInstanceCreate(AnimStateInstance* pInst);
	void AnimStateInstanceDestroy(AnimStateInstance* pInst);
	void AnimStateInstancePendingChange(StringId64 layerId,
										StateChangeRequest::ID requestId,
										StringId64 changeId,
										int changeType);
	bool AnimStateInstanceAlignFunc(const AnimStateInstance* pInst,
									const BoundFrame& prevAlign,
									const BoundFrame& currAlign,
									const Locator& apAlignDelta,
									BoundFrame* pAlignOut,
									bool debugDraw);
	void AnimStateInstanceIkFunc(const AnimStateInstance* pInst, AnimPluginContext* pPluginContext, const void* pParams);
	void AnimStateInstanceDebugPrintFunc(const AnimStateInstance* pInst, StringId64 debugType, IStringBuilder* pText);

	static void AnimStateInstancePrepareCallback(uintptr_t userData,
												 StringId64 layerId,
												 StateChangeRequest::ID requestId,
												 AnimStateInstance::ID instId,
												 bool isTop,
												 const DC::AnimState* pAnimState,
												 FadeToStateParams* pParams)
	{
		NdGameObject* pGo = (NdGameObject*)userData;
		pGo->AnimStateInstancePrepare(layerId, requestId, instId, isTop, pAnimState, pParams);
	}

	static void AnimStateInstanceCreateCallback(uintptr_t userData, AnimStateInstance* pInst)
	{
		NdGameObject* pGo = (NdGameObject*)userData;
		pGo->AnimStateInstanceCreate(pInst);
	}

	static void AnimStateInstanceDestroyCallback(uintptr_t userData, AnimStateInstance* pInst)
	{
		NdGameObject* pGo = (NdGameObject*)userData;
		pGo->AnimStateInstanceDestroy(pInst);
	}

	static void AnimStateInstancePendingChangeCallback(uintptr_t userData,
													   StringId64 layerId,
													   StateChangeRequest::ID requestId,
													   StringId64 changeId,
													   int changeType)
	{
		NdGameObject* pGo = (NdGameObject*)userData;
		pGo->AnimStateInstancePendingChange(layerId, requestId, changeId, changeType);
	}

	static bool AnimStateInstanceAlignFuncCallback(uintptr_t userData,
												   const AnimStateInstance* pInst,
												   const BoundFrame& prevAlign,
												   const BoundFrame& currAlign,
												   const Locator& apAlignDelta,
												   BoundFrame* pAlignOut,
												   bool debugDraw)
	{
		NdGameObject* pGo = (NdGameObject*)userData;
		return pGo->AnimStateInstanceAlignFunc(pInst, prevAlign, currAlign, apAlignDelta, pAlignOut, debugDraw);
	}

	static void AnimStateInstanceIkFuncCallback(uintptr_t userData,
												const AnimStateInstance* pInst,
												AnimPluginContext* pPluginContext,
												const void* pParams)
	{
		NdGameObject* pGo = (NdGameObject*)userData;

		// Make sure the userData matches the plugin context game object, which it doesn't for copy remap layers
		if (pGo == pPluginContext->m_pContext->m_pAnimDataHack->m_hProcess.ToProcess())
			pGo->AnimStateInstanceIkFunc(pInst, pPluginContext, pParams);
	}

	static void AnimStateInstanceDebugPrintFuncCallback(uintptr_t userData,
														const AnimStateInstance* pInst,
														StringId64 debugType,
														IStringBuilder* pText)
	{
		NdGameObject* pGo = (NdGameObject*)userData;
		pGo->AnimStateInstanceDebugPrintFunc(pInst, debugType, pText);
	}

	virtual const WristInfo* GetWristInfo() const { return nullptr; }
	virtual const AnkleInfo* GetAnkleInfo() const { return nullptr; }

	virtual Point GetGroundPosPs() const { return GetTranslationPs(); }
	virtual Vector GetGroundNormalPs() const { return kUnitYAxis; }

	Point GetGroundPosWs() const { return GetParentSpace().TransformPoint(GetGroundPosPs()); }
	Vector GetGroundNormalWs() const { return GetParentSpace().TransformVector(GetGroundNormalPs()); }

	virtual EmotionControl* GetEmotionControl() { return nullptr; }
	virtual const EmotionControl* GetEmotionControl() const { return nullptr; }
	virtual StringId64 GetEmotionMapId() const { return SID("*emotion-map*"); }
	virtual StringId64 GetBaseEmotion() const { return INVALID_STRING_ID_64; }

	virtual const AnimOverlaySnapshot* GetAnimOverlaysForWeapon(NdGameObjectHandle hWeapon) const;

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Foot Ik
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual FootIkCharacterType GetFootIkCharacterType() const { return kFootIkCharacterTypeHuman; }
	bool IsQuadruped() const { return ::IsQuadruped(GetFootIkCharacterType()); }
	int GetLegCount() const { return ::GetLegCountForCharacterType(GetFootIkCharacterType()); }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Async Animation Callbacks
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void PostAnimUpdate_Async();
	virtual void PostAnimBlending_Async();
	virtual void PostJointUpdate_Async();
	virtual void PostJointUpdatePaused_Async() {}

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Props/Cloth
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual NdPropControl* GetNdPropControl() { return nullptr; }
	virtual const NdPropControl* GetNdPropControl() const { return nullptr; }

	virtual void ClothUpdate_Async();

	/// --------------------------------------------------------------------------------------------------------------- ///
	// State Scripts
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual Err InitFromScriptOptions(const DC::NdSsOptions& opts);

	// Optional state script instance, present if a state script id was specified on the spawner in Charter.
	virtual const SsInstance* GetScriptInstance() const override { return m_pScriptInst; }
	virtual SsInstance* GetScriptInstance() override { return m_pScriptInst; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Serialization
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void Serialize(BitStream&) const override;
	virtual void Deserialize(BitStream&) override;

	/// --------------------------------------------------------------------------------------------------------------- ///
	// IGC Animation
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool AllowIgcAnimation(const SsAnimateParams& params) const { return true; }
	void OnPlayIgcAnimation(StringId64 name, StringId64 animId, F32 durationSec);
	void OnStopIgcAnimation(StringId64 name, StringId64 animId);
	StringId64 GetPlayingIgcName() const;
	StringId64 GetPlayingIgcAnimation() const;
	F32 GetPlayingIgcElapsedTime() const;
	I16 FindJointIndex(const StringId64 name) const;
	bool DrawPlayingIgc();
	virtual Color GetIgcLogColor() const { return kColorYellow; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Vox
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual NdVoxControllerBase& GetVoxController();
	const NdVoxControllerBase& GetVoxControllerConst() const;

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Foliage
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void SyncFoliageSfxController(void);		// Cannot be private but should only be called by AudioManager

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Attach System
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void SetupAttachSystem(const ProcessSpawnInfo& spawn);
	const AttachSystem* GetAttachSystem() const;			//<! ptr to attach system or NULL
	AttachSystem* GetAttachSystem();			//<! ptr to attach system or nullptr
	bool TryFindAttachPoint(AttachIndex* pAttach, StringId64 id) const;
	AttachIndex FindAttachPoint(StringId64 id) const;
	void SetAttachable(AttachIndex attachIndex, StringId64 attachableId);
	StringId64 GetAttachable(AttachIndex attachIndex) const;

	struct AttachOrJointIndex
	{
		I16			m_jointIndex;	// only one of these will be valid
		AttachIndex	m_attachIndex;	// only one of these will be valid

		AttachOrJointIndex() : m_jointIndex(-1), m_attachIndex(AttachIndex::kInvalid) { }
		bool IsJoint() const	{ return (m_jointIndex >= 0); }
		bool IsAttach() const	{ return (m_attachIndex != AttachIndex::kInvalid); }
		bool IsValid() const	{ return (IsJoint() || IsAttach()); }
	};

	AttachOrJointIndex FindAttachOrJointIndex(StringId64 attachOrJointId) const;

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Targetable
	/// --------------------------------------------------------------------------------------------------------------- ///
	const Targetable* GetTargetable() const;
	Targetable* GetTargetable();
	void SetTargetable(Targetable* p);

	bool TurnReticleRed() const { return m_goFlags.m_bTurnReticleRed; }
	void SetTurnReticleRed(bool bTurnRed);
	virtual void SetTrackableByObjectId(bool bValue);

	//add all attach points as targetable spots
	//assumes attach points have already been set up
	void MakeTargetable();

	virtual void OnPotentialTargetRegistered() const { }
	virtual void OnPotentialTargetUnregistered() const { }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Physics
	/// --------------------------------------------------------------------------------------------------------------- ///

	// Inits single rigid body for this process. Collision object from current look is used.
	Err InitRigidBody(RigidBody& body);

	virtual Err InitCompositeBody(CompositeBodyInitInfo* pInitInfo = nullptr);		// by default this is not called -- call from your Init() if you want a CompositeBody (or create one yourself)

	CompositeBody* GetCompositeBody() { return m_pCompositeBody; }
	const CompositeBody* GetCompositeBody() const { return m_pCompositeBody; }
	virtual RigidBody* GetRigidBody() const { return nullptr; }				// derived classes that use RigidBody instead of CompositeBody should override this

	// In case we need to treat this object as 1-body collision and we want to support deformable bodies (that need composite body)
	RigidBody* GetSingleRigidBody() const;

	virtual bool CanHaveCloth() { return true; }
	virtual void SetPhysMotionType(RigidBodyMotionType motionType, F32 blend = 0.0f);
	void SetPhysMotionTypeEvent(RigidBodyMotionType motionType, Event& event);
	RigidBodyMotionType GetPhysMotionType() const;
	void SetPhysLayer(Collide::Layer layer);

	virtual void SetTeleport();

	U32F GetHavokSystemId() const;
	void FreeHavokSystemId();
	void TakeOverHavokSystemId(NdGameObject* pOther);

	// locator world space get accessors
	virtual const Vector GetVelocityWs() const { return m_velocityWs; }
	virtual const Vector GetVelocityPs() const { return m_velocityPs; }
	const Vector GetBaseVelocityPs() const { return m_velocityPs; }
	const Vector GetAngularVelocityWs() const { return m_angularVecWs; }
	const Vector GetVelocityAtPoint(Point_arg posWs) const
	{
		return GetVelocityWs() + Cross(GetAngularVelocityWs(), posWs - GetTranslation());
	}

	virtual void OnBodyKilled(RigidBody& body) {} // called whenever a RigidBody gets killed

	void SetSoundPhysFxDelayTime(F32 f) { m_soundPhysFxDelayTime = f; }
	float GetSoundPhysFxDelayTime() { return m_soundPhysFxDelayTime; }

	SfxProcess* PlayPhysicsSound(const SfxSpawnInfo& info, bool playFirstRequest = false, bool destructionFx = false, bool playAlways = false);
	void InitPhysFx(RigidBody& rigidBody);									// derived classes use this if a single RigidBody is created manually
	virtual const DC::PhysFxSet* GetPhysFxSet() const;
	void DelayPhysicsSounds(F32 delayInSeconds);

	// searches through the part meshes for any cloth and adds it to the CompositeBody
	void FindClothPrototypes(CompositeBodyInitInfo& info);

	// Looks for single collision object in current look. If there are more collisions first will be returned arbitrarily and warning printed
	const ArtItemCollision* GetSingleCollision() const;

	// Get linear velocity from the platform at the position of this object
	Vector GetPlatformLinearVelocity() const;

	// Convert a velocity back and forth between "ridden platform space" and world space.
	// If mode == kIncludePlatformVelocity, the world-space velocity is assumed to include the
	// velocity of the platform (so this velocity is subtracted out when converting to platform space).
	// If mode == kNoPlatformVelocity, the velocity vector is simply rotated between the two spaces.
	enum ConvertMode { kNoPlatformVelocity, kIncludePlatformVelocity };
	Vector ConvertVelocityWsToPs(Vector_arg velocityWs, ConvertMode mode) const;
	Vector ConvertVelocityPsToWs(Vector_arg velocityPs, ConvertMode mode) const;

	// Convert a locator back and forth between "ridden platform space" and world space.
	Locator ConvertLocatorWsToPs(const Locator& locWs) const;
	Locator ConvertLocatorPsToWs(const Locator& locPs) const;

	// Returns true if the object is in water. Derived classes will override.
	virtual bool IsInWater() const { return false; }
	virtual float WaterDepth() const { return 0.0f; }
	virtual float WaterSurfaceY() const { return -kLargestFloat; }

	virtual bool IsBreakableGlass() const { return false; }
	// This seems questionable... should be in derived class(es) that are breakable...
	void DisintegrateBreakable(Vector_arg dir, F32 impulse, bool killDynamic = false, bool noFx = false);
	void DisintegrateBreakableExplosion(Point_arg center, F32 impulse);
	void CollapseBreakable();
	void SetDestructionEnabledEvent(bool enable, Event& event);
	void SetDestructionEnabledForContactsEvent(bool enable, Event& event);

	// How much softstep is applied to given object assumig we are character or its prop or something that is applying the soft step
	virtual F32 GetCharacterSoftStepMultiplier(const RigidBody* pCollidingBody) const { return 1.0f; }

	class RigidBodyIterator
	{
	public:
		RigidBodyIterator(): m_pGameObject(nullptr) {};
		const RigidBody* First(const NdGameObject* pGameObject);
		const RigidBody* Next();
		U32 LastRigidBodyIndex() const { return m_rbIndex-1; }
		static U32 GetCount(const NdGameObject* pGameObject);
		const NdGameObject* m_pGameObject;
		U32 m_rbIndex;
	};

	class MutableRigidBodyIterator
	{
	public:
		MutableRigidBodyIterator() : m_pGameObject(nullptr) {};
		RigidBody* First(NdGameObject* pGameObject);
		RigidBody* Next();
		U32 LastRigidBodyIndex() const { return m_rbIndex - 1; }
		static U32 GetCount(const NdGameObject* pGameObject);
		NdGameObject* m_pGameObject;
		U32 m_rbIndex;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Faction
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void SetFactionId(FactionId fac);
	FactionId GetFactionId() const { return m_faction; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Splashers
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool SplashersEnabled(const SpawnInfo& spawnData);
	virtual void InitializeSplashers(StringId64 skeletonId, StringId64 setlistId);
	virtual void CopySplasherState(const NdGameObject* pSource);

	const SplasherSkeletonInfo* GetSplasherSkeleton() const;
	SplasherSkeletonInfo* GetSplasherSkeleton();
	const SplasherSet* GetSplasherSet() const;
	SplasherSet* GetSplasherSet();
	const SplasherController* GetSplasherController() const;
	SplasherController* GetSplasherController();

	StringId64 GetSplasherSkeletonId() const { return m_splasherSkeletonId; }
	StringId64 GetSplasherSetlistId() const { return m_splasherSetlistId; }

	virtual StringId64 GetSplasherObjectState() const
	{
		return (m_effSplasherObjectState != INVALID_STRING_ID_64) ? m_effSplasherObjectState : GetStateId();
	}
	StringId64 GetSplasherObjectStateEffOverride() const
	{
		return m_effSplasherObjectState;
	}
	StringId64 m_effSplasherObjectState = INVALID_STRING_ID_64;
	StringId64 m_effSplasherObjectStateNext = INVALID_STRING_ID_64;

	virtual bool FeetSplashersInWater() const	{ return m_feetSplashersInWater; }
	void SetFeetSplashersInWater(bool inWater) { m_feetSplashersInWater = inWater; }
	virtual void UpdateFeetSplashersInWater(const SplasherSkeletonInfo* pSplasherSkel);

	virtual bool AreFeetWet() const { return m_feetWet; }
	void SetFeetWet(bool wet) { m_feetWet = wet; }

	virtual bool IsSwimming() const	{ return false; }
	virtual bool IsProneUnderwater() const	{ return false; }
	virtual bool IsDynamicTapRegistered() const { return false; }

	virtual void SetHasGasMask(StringId64 futzId = INVALID_STRING_ID_64) { m_goFlags.m_gasMaskMesh = true; m_gasMaskFutz = futzId; }
	virtual StringId64 GetGasMaskFutz() const { return m_goFlags.m_gasMaskMesh ? m_gasMaskFutz : INVALID_STRING_ID_64; }
	virtual void ClearHasGasMask() { m_goFlags.m_gasMaskMesh = false; m_gasMaskFutz = INVALID_STRING_ID_64; }

	virtual bool HasHorse() const { return false; }

	// sadly in NdGameObject so that simple objects can ride both real and fake horses
	bool HorseGetSaddleApRef(BoundFrame& outApRef, DC::HorseRiderPosition riderPos) const;
	bool HorseGetSaddleApRef(Locator& outApRef, DC::HorseRiderPosition riderPos) const;

	static void InitSplasherJobs();
	static void DoneSplasherJobs();
	static void WaitForAllSplasherJobs();

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Navigation - ways this object can influence the navigation of others
	/// --------------------------------------------------------------------------------------------------------------- ///

	virtual Err InitNavBlocker(const ProcessSpawnInfo& spawn);

	virtual void UpdateNavBlocker();
	virtual void UpdateNavBlockerEnabled();
	virtual void SetNavBlockerPolyFromCollision(DynamicNavBlocker* pBlocker, const CompositeBody* pCompositeBody);
	virtual void SetNavBlockerPolyFromCollision(DynamicNavBlocker* pBlocker,
												const Locator& locWs,
												const CompositeBody* pCompositeBody);

	const DynamicNavBlocker*	GetNavBlocker() const	{ return m_pDynNavBlocker; }
	DynamicNavBlocker*			GetNavBlocker()			{ return m_pDynNavBlocker; }

	const StaticNavBlocker*		GetStaticNavBlocker() const { return m_pStaticNavBlocker; }
	StaticNavBlocker*			GetStaticNavBlocker() { return m_pStaticNavBlocker; }


	void EnableNavBlocker(bool enable);
	bool IsNavBlockerEnabled() const { return m_wantNavBlocker; }
	void SetNavBlocker(DynamicNavBlocker* p); // can only be called once

	// for giving script control of allocating and deallocating nav blockers on big levels
	void AllocateDynamicNavBlocker();
	void DeallocateDynamicNavBlocker();

	// Block or unblock the nav poly on which the object is sitting. Useful for doorways. Do not
	// confuse with EnableNavBlocker();
	void BlockNavPoly(bool block);
	bool IsNavPolyBlocked() const;

	/// --------------------------------------------------------------------------------------------------------------- ///
	// PlatformControl - for objects that need to drag navmeshes around with them
	/// --------------------------------------------------------------------------------------------------------------- ///
	Err InitPlatformControl(bool basicInitOnly	   = false,
							bool suppressTapErrors = false,
							StringId64 bindJointIdOverride = INVALID_STRING_ID_64);
	void DisablePlatformControl(); // destroy platform control
	void						ForcePlatformControlRegisterTaps();
	virtual void				OnPlatformMoved(const Locator& deltaLoc); // called when the platform we are riding moved
	const PlatformControl*		GetPlatformControl() const		{ return m_pPlatformControl; }
	PlatformControl*			GetPlatformControl()			{ return m_pPlatformControl; }
	bool						ForceAllowNpcBindings() const	{ return m_goFlags.m_forceAllowNpcBinding; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Action Packs
	/// --------------------------------------------------------------------------------------------------------------- ///
	void 						RegenerateCoverActionPacks(); // issue a request to re-generate cover APs for this object
	void 						RemoveCoverActionPacks(I32F bodyJoint = -1); // immediately remove cover APs generated by this obj. If bodyJoint is specified, only covers generated from body on this joint will be removed.
	void 						RegisterCoverActionPacksInternal(const NdGameObject* pBindObj, const RigidBody* pBindTarget); // used by PlatformControl
	virtual IApBlocker*			GetApBlockerInterface() { return nullptr; }

	void						InitTextureInstanceTable();
	void						DestroyTextureInstanceTable();

	void						HandleDataLoggedOutError();

protected:
	void						PostSnapshotInit() override;

	void						InitJointWind();
	void						UpdateJointWind();
	void						UpdateJointWindNoAnim(const ndanim::JointParams* pJointLsSource);
	void						SetupJointWindInput();
	void						ApplyJointWindOutput(const ndanim::JointParams* pJointLsSource, Locator* pWsLocsOut, ndanim::JointParams* pJointLsOut);

private:
	// implementation
	void 						UnregisterAllActionPacks();
	virtual Err					SetupPlatformControl(const SpawnInfo& spawn);

	void						LogRetargetedAnimations() const;
public:

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Parenting (Process Tree)
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void					ChangeParentProcess(Process* pNewParent, bool pushLastChild = false) override;

	/// --------------------------------------------------------------------------------------------------------------- ///
	/// AttachmentCtrl
	/// --------------------------------------------------------------------------------------------------------------- ///
	bool IsAttached() const { return m_pAttachmentCtrl ? m_pAttachmentCtrl->IsAttached() : false; }
	const NdGameObject* GetParentGameObject() const { return m_pAttachmentCtrl ? m_pAttachmentCtrl->GetParent().ToProcess() : nullptr; }
	NdGameObject* GetParentGameObjectMutable() { return m_pAttachmentCtrl ? m_pAttachmentCtrl->GetParentMutable().ToMutableProcess() : nullptr; }
	NdGameObjectHandle GetParentObjHandle() const { return m_pAttachmentCtrl ? m_pAttachmentCtrl->GetParent() : NdGameObjectHandle(); }
	MutableNdGameObjectHandle GetParentObjHandleMutable() const { return m_pAttachmentCtrl ? m_pAttachmentCtrl->GetParentMutable() : MutableNdGameObjectHandle(); }

	Locator GetParentAttachOffset() const { return m_pAttachmentCtrl ? m_pAttachmentCtrl->GetParentAttachOffset() : Locator(kIdentity); }
	float GetParentAttachOffsetBlendTime() const { return m_pAttachmentCtrl ? m_pAttachmentCtrl->GetParentAttachOffsetBlendTime() : 0.f; }
	Locator GetDesiredParentAttachOffset() const { return m_pAttachmentCtrl ? m_pAttachmentCtrl->GetDesiredParentAttachOffset() : Locator(kIdentity); }

	AttachIndex GetParentAttachIndex() const { return m_pAttachmentCtrl ? m_pAttachmentCtrl->GetParentAttachIndex() : AttachIndex::kInvalid; }
	I32 GetAttachJointIndex() const { return m_pAttachmentCtrl ? m_pAttachmentCtrl->GetAttachJointIndex() : kInvalidJointIndex; }
	virtual void UpdateAttached();

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Orientation
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual Vector					GetUp() const; // prerequisite for GetDown()

	virtual GroundHugController*	GetGroundHugController() const { return nullptr; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Binding (BoundFrame Attachments)
	/// --------------------------------------------------------------------------------------------------------------- ///

	// Returns a rigid body to which child objects can bind.
	virtual const RigidBody*	GetJointBindTarget(StringId64 jointId) const;

	// Returns the default rigid body to which child objects should bind if they don't explicitly specify a joint.
	const RigidBody*			GetDefaultBindTarget() const { return GetJointBindTarget(INVALID_STRING_ID_64); }

	// Returns whether this game object is ready to be a "bind target" for a particular child game object.
	bool							IsValidBindTarget(const SpawnInfo* pForChildInfo = nullptr) const;
	// Hook called when this object first becomes a valid bind target.
	void							OnIsValidBindTarget();

	// Returns the world-space bind pose locator of the given joint, which is the "bind space" of that joint's
	// RigidBody at spawn time (i.e. before animation moves the joints).
	Locator							GetBindLocator(StringId64 jointId = INVALID_STRING_ID_64) const;

	// Enable or disable freezing of the binding when the collision geometry (RigidBody) goes away.
	void							SetBindingFrozenWhenNonPhysical(bool freeze);
	bool							IsBindingFrozenWhenNonPhysical() const { return m_bFreezeBindingWhenNonPhysical; }

	// PLATFORM BINDINGS

	// Returns the bind target of this object's PlatformControl, iff it has one.
	const RigidBody*			GetPlatformBindTarget() const;

	void							SetIsPlatform(bool is) { m_goFlags.m_bIsPlatform = is; }
	bool							IsPlatform() const { return m_goFlags.m_bIsPlatform; }

	// Returns the "platform" on which this game object is riding (or itself if it is a moving platform).
	// Returns NULL if the object is not riding on a platform.
	virtual NdGameObject*			GetBoundPlatform() const;

	// Iff this object is ultimately riding on a platform, return the rigid body it is riding on.
	// e.g. if called on a character standing on a crate attached to a vehicle, this function would return
	// the rigid body (within the vehicle) to which the CRATE is attached.
	const RigidBody*			GetPlatformBoundRigidBody() const;

	// NEW BINDING INTERFACE

	Binding							GetJointBinding(StringId64 jointId) const;
	Binding							GetDefaultBinding() const { return GetJointBinding(INVALID_STRING_ID_64); }

	Binding							GetPlatformBinding() const;

	void							SetEnablePlayerParenting(bool enableParenting) { m_goFlags.m_disablePlayerParenting = !enableParenting; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// FeatureDb
	/// --------------------------------------------------------------------------------------------------------------- ///
	void EnableFeatures()	{ m_bDisableFeatureDbs = false; }
	void DisableFeatures() { m_bDisableFeatureDbs = true; }
	bool AreFeaturesEnabled() const { return !m_bDisableFeatureDbs; }
	void SetFeaturesEnabledForAnyLayer(bool b) { m_bEnableFeatureDbsForAnyLayer = b; }
	bool GetFeaturesEnabledForAnyLayer() const { return m_bEnableFeatureDbsForAnyLayer; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Spline Following
	/// --------------------------------------------------------------------------------------------------------------- ///
	static F32 GetInvalidSplineNotifyDistance() { return kInvalidSplineNotifyDistance; }
	virtual SplineTracker* GetSplineTracker() const { return m_pSplineTracker; }
	virtual void InitSplineTracker();
	virtual void UpdateSplineTracker();
	virtual bool NotifyAtSplineDistance(F32 distance_m, Process& notifyProcess, U32 notifyData);
	F32 GetSplineNotifyDistance() { return m_splineNotifyDistance_m; }
	void ClearSplineNotify() { m_splineNotifyDistance_m = kInvalidSplineNotifyDistance; m_hNotifySpline = nullptr; }
	SsAction& GetAtSplineDistanceAction() { return m_ssAtSplineDistanceAction; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Regions
	/// --------------------------------------------------------------------------------------------------------------- ///
	void SetRegionControl(RegionControl* p);
	RegionControl* GetRegionControl() const;		//<! ptr to region control or NULL
	bool IsInRegion(StringId64 regionId) const;
	virtual StringId64 GetRegionActorId() const;

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Grid Hash
	/// --------------------------------------------------------------------------------------------------------------- ///
	SimpleGridHashId GetGridHashId() const { return m_gridHashId; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Attack Handling
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool NeedsAttackHandler() const { return true; }
	NdAttackHandler* GetAttackHandler() { return m_pAttackHandler; }
	const NdAttackHandler* GetAttackHandler() const { return m_pAttackHandler; }
	virtual void GetReceiverDamageInfo(ReceiverDamageInfo* pInfo) const;
	virtual StringId64 GetDamageReceiverClass(const RigidBody* pBody = nullptr, bool* pHitArmor = nullptr) const;

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Network
	/// --------------------------------------------------------------------------------------------------------------- ///
	bool HasNetController() const { return m_pNetController != nullptr; }
	NdNetController& GetNetController()
	{
		ALWAYS_ASSERT(m_pNetController);
		return *m_pNetController;
	}
	const NdNetController& GetNetController() const
	{
		ALWAYS_ASSERT(m_pNetController);
		return *m_pNetController;
	}
	virtual bool NeedsNetController(const SpawnInfo& spawn) const;
	virtual bool ShouldCreateNetSnapshotController(const SpawnInfo& spawn) const;
	virtual bool ShouldCreateNetPhaseSnapshotController(const SpawnInfo& spawn) const;
	virtual bool CanTransferOwnership() const { return false; }
	virtual bool AllowNetPickup() const { return true; }
	virtual void OwnershipRequestDenied() {}
	virtual NdNetPlayerTracker* GetNdNetOwnerTracker() const;
	virtual NetPlayerTracker* GetNetOwnerTracker() const;
	virtual U32 GetNetOwnerTrackerId() const;
	virtual bool ManualLocatorUpdate() const { return false; }
	virtual void WriteSimpleSnapshotExtraData(BitStream* bitStream) const {}
	virtual void ReadSimpleSnapshotExtraData(BitStream* bitStream) {}

#ifdef HEADLESS_BUILD
	bool HasNetLodController() const { return m_pNetLodController != nullptr; }
	NdNetLodController& GetNetLodController()
	{
		ALWAYS_ASSERT(m_pNetLodController);
		return *m_pNetLodController;
	}
	const NdNetLodController& GetNetLodController() const
	{
		ALWAYS_ASSERT(m_pNetLodController);
		return *m_pNetLodController;
	}
#else
	bool HasNetLodController() const { return false; }
#endif

	virtual bool NeedsNetLodController(const SpawnInfo& spawn) const;

	// Convenience Hooks to NdNetController API
	virtual void SetNetOwnerTracker(NdNetPlayerTracker* pNewOwner);
	virtual bool IsNetOwnedByMe() const;
	bool RequestNetOwnership(NdNetPlayerTracker* pNewOwner) const;
	void UpdateNetOwnership();
	void UpdateLocatorNet();

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Snapshot
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual ProcessSnapshot*		AllocateSnapshot() const override;
	virtual void					RefreshSnapshot(ProcessSnapshot* pSnapshot) const override;

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Tag interface
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool	GetTagDataSymbol(StringId64 tagId, StringId64& dataId) const override;
	virtual bool	GetTagDataString(StringId64 tagId, const char** ppDataString) const override;
	virtual bool	GetTagDataFloat(StringId64 tagId, F32& dataFloat) const override;
	virtual bool	GetTagDataInt(StringId64 tagId, I32& dataInt) const override;
	virtual bool	GetTagDataBoolean(StringId64 tagId, bool& dataBool) const override;

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Instance Texture interface
	/// --------------------------------------------------------------------------------------------------------------- ///
	InstanceTextureTable*		GetInstanceTextureTable(int index);
	virtual I32					GetWetMaskIndex(U32 instanceTextureTableIndex = 0) const;
	virtual void				SetWetMaskIndex(I32 newWetMaskIndex, U32 instanceTextureTableIndex = 0);
	virtual I32					GetMudMaskIndex() const { return GetWetMaskIndex(); }
	virtual FxRenderMaskHandle	GetBloodMaskHandle() const { return kFxRenderMaskHandleInvalid; }
	virtual void				SetBloodMaskHandle(FxRenderMaskHandle hdl) { }
	virtual void				OnBloodMaskEffectAdded() { }

	virtual StringId64			GetBodyPartToBloodFxTableName() const { return INVALID_STRING_ID_64; }

	void						AddInstanceTexture(I32 index, I32 textureType, ndgi::Texture hTexture);
	void						AddInstanceTexture(I32 tableIndex, I32 textureType, LoadedTexture* pTexture);
	void						AddInstanceTextureOnAllTextureTables(I32 textureType, ndgi::Texture hTexture);
	void						AddInstanceTextureOnAllTextureTables(I32 textureType, LoadedTexture* pTexture);
	I32							GetNumInstanceTextureTables() const { return m_numInstanceTextureTables; }
	virtual I32					GetInstanceTextureTableIndexForMesh(int meshIndex) const { return 0; }
	virtual void				ApplyOverlayInstanceTextures();

	virtual void				SetFgInstanceFlag(FgInstance::FgInstanceFlags flag);
	virtual void				ClearFgInstanceFlag(FgInstance::FgInstanceFlags flag);
	virtual bool				IsFgInstanceFlagSet(FgInstance::FgInstanceFlags flag);

	bool						NeedsWetmaskInstanceTexture() const { return m_goFlags.m_needsWetmaskInstanceTexture; }

	enum DynamicMaskType
	{
		kDynamicMaskTypeNone,
		kDynamicMaskTypeWet,
		kDynamicMaskTypeBlood,
	};
	virtual I32 GetDynamicMaskType() const { return kDynamicMaskTypeNone; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool IsDoor() const { return false; }
	virtual bool IsBuoyancyPallet() const { return false; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// fix ladder has 2 grab points.
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool AdjustNearbyObjectProbes(const NdGameObject* pPlayer, Point* adjustedStart, Point* adjustedEnd) const { return false; }
	virtual const Pat* GetLadderPat() const { return nullptr; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Probe weight control
	/// --------------------------------------------------------------------------------------------------------------- ///

	virtual void		SetProbeTreeWeight(StringId64 levelNameId, float targetWeight, float changeSpeed);
	void				UpdateProbeTreeWeightControls(float deltat);
	void				ApplyProbeTreeWeights();
	void				FreeProbeTreeWeightControls();

	/// --------------------------------------------------------------------------------------------------------------- ///
	JointOverrideData* GetJointOverrideData() { return m_pJointOverrideData; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool		CanPlayerBeParented() const;

	/// --------------------------------------------------------------------------------------------------------------- ///
	float GetInitialAmbientScale() const { return m_initialAmbientScale > 0.0f ? m_initialAmbientScale : 1.0f; }

	void UpdateJointShaderDrivers();

	virtual bool					IsVisibleInListenMode() const { return m_goFlags.m_isVisibleInListenMode; }
	virtual bool					ShouldShowAttachablesInListenMode() const { return true; }
	virtual void					RegisterInListenModeMgr(U32 addFlags = 0, float fadeTime = 0.0f);
	virtual void					UnregisterFromListenModeMgr(float fadeTime = 0.0f);

	void SetEnableFgVis(bool enable);
	bool IsFgVisEnabled() const { return !m_goFlags.m_disableFgVis; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Custom attach points
	/// --------------------------------------------------------------------------------------------------------------- ///
	U32F CountCustomAttachPoints(const DC::CustomAttachPointSpecArray* pSpecArray) const;
	void AddCustomAttachPoints(const DC::CustomAttachPointSpecArray* pSpecArray);
	void InitCustomAttachPoints(StringId64 nameId);

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Debugging
	/// --------------------------------------------------------------------------------------------------------------- ///
	struct DebugDrawFilter
	{
	public:
		bool m_drawFocusable;
		bool m_drawTargetable;
		bool m_drawAnimConfig;
		bool m_drawProcessedAnimSegments;
		bool m_drawAnimControl;
		bool m_printAnimControl;				// Prints once to the MsgAnim instead of printing on screen
		bool m_recordAnimControl;				// Record anim control to the JAF Anim Recorder
		bool m_drawAnimOverlays;
		bool m_drawAlign;
		bool m_drawApReference;
		bool m_drawApReferenceParenting;
		F32 m_apReferenceScale;
		bool m_drawApReferenceFromAllStates;
		bool m_noAlignWhenDrawApReference;
		bool m_drawChannelLocators;
		bool m_drawFloatChannels;
		bool m_drawRenderXform;
		bool m_drawNumericAlignAp;
		bool m_drawNumericAlignApCharterAndMaya;
		bool m_drawSkeletonWorldLocs;
		bool m_drawSkeletonSkinningMats;
		bool m_drawOpenings;
		bool m_drawAnalogDoorOpenings;
		bool m_drawBigBreakableOpenings;
		bool m_drawDebrisObjectOpenings;
		bool m_drawLightJoints;
		bool m_drawNavLocation;
		bool m_drawPointOfInterest;
		bool m_drawPointOfInterestMinCollectRadius;
		bool m_drawPointOfInterestMaxCollectRadius;
		bool m_drawInBindPose;
		bool m_drawLightJointNames;
		bool m_drawSplineTracker;
		bool m_drawRegions;

		struct Joints
		{
			Joints()
				: m_debugDraw(false)
				, m_rangeStart(0)
				, m_rangeEnd(-1)
				, m_segmentRangeStart(0)
				, m_segmentRangeEnd(0)
				, m_drawNames(false)
				, m_offsetBaseIndex(-1)
				, m_transX(0.0f)
				, m_transY(0.0f)
				, m_transZ(0.0f)
				, m_rotX(0.0f)
				, m_rotY(0.0f)
				, m_rotZ(0.0f)
			{
				m_filter1[0] = 0;
				m_filter2[0] = 0;
				m_filter3[0] = 0;
				m_filter4[0] = 0;
			}

			bool m_debugDraw;
			I32 m_rangeStart;
			I32 m_rangeEnd;
			I32 m_segmentRangeStart;
			I32 m_segmentRangeEnd;

			I32 m_offsetBaseIndex;
			F32 m_transX;
			F32 m_transY;
			F32 m_transZ;
			F32 m_rotX;
			F32 m_rotY;
			F32 m_rotZ;

			bool m_drawNames;

			char m_filter1[256];
			char m_filter2[256];
			char m_filter3[256];
			char m_filter4[256];
		};
		Joints m_debugJoints;

		bool m_drawSelectedJoint;
		I32 m_jointIndex;
		bool m_drawJointParams;
		bool m_drawJointParent;
		bool m_drawJointsInJointCache;
		I32 m_numParentJointsToDraw;
		SMath::Quat::RotationOrder m_drawJointParamsRotOrder;
		F32 m_jointScale;

		struct AttachPoints
		{
			AttachPoints()
				: m_debugDraw(false)
				, m_drawSelected(false)
				, m_selectedIndex(0)
			{
				m_filter1[0] = 0;
				m_filter2[0] = 0;
				m_filter3[0] = 0;
			}

			bool m_debugDraw;
			bool m_drawSelected;
			I32 m_selectedIndex;
			char m_filter1[256];
			char m_filter2[256];
			char m_filter3[256];
		};
		AttachPoints m_debugAttachPoints;

		bool m_drawBoundingSphere;
		bool m_drawBoundingBox;
		bool m_drawLookNames;
		bool m_drawMeshNames;
		bool m_drawAmbientOccludersId;
		bool m_drawFgInstanceSegmentMasks;
		bool m_drawStateScripts;
		bool m_drawAssociatedLevel;
		bool m_drawSpecCubemapInfo;
		bool m_drawFaction;
		bool m_drawNavBlocker;
		bool m_drawCompositeBody;
		bool m_drawParentSpace;
		bool m_drawScale = false;
		bool m_drawRiders;
		bool m_drawEntityDB;
		bool m_drawState;
		bool m_drawBucket;
		bool m_skelAndAnim;
		bool m_drawChildScriptEvents;

		bool m_drawDiffuseTintChannels;
		bool m_drawingDiffuseTintChannels;

		bool m_writeEffects;
		bool m_writeEffectsToPerforceFile;
		bool m_includeExistingEffects;
		bool m_includeNewEffects;
		bool m_generateFootEffects;

		bool m_visualEffDebugging;
		F32 m_visualEffDebuggingTime;

		bool m_drawCharacterCollisionCapsule;

		bool m_drawFeetWet;
		bool m_drawHighContrastModeTag;

	public:
		DebugDrawFilter()
		{
			memset(this, 0, sizeof(*this));
			m_visualEffDebuggingTime = 0.5f;
			m_jointScale = 1.0f;
			m_debugJoints.m_rangeEnd = -1;
			m_debugJoints.m_offsetBaseIndex = -1;
			m_apReferenceScale = 1.0f;
			m_drawAnalogDoorOpenings = true;
			m_drawBigBreakableOpenings = true;
			m_drawDebrisObjectOpenings = true;
			m_drawJointsInJointCache = false;
		}

		bool ShouldDebugDraw() const;
	};

	virtual void DebugDraw(const DebugDrawFilter& filter) const;
	virtual void DebugShowProcess(ScreenSpaceTextPrinter& printer) const override;
	virtual void DebugDrawCompositeBody() const;

	virtual void DebugShowBodyMarkers();
	void DebugShowApReference(bool drawNumericAlignAp = false,
							  bool drawNumericAlignApCharterAndMaya = false,
							  bool drawApReferenceParenting			= false,
							  F32 scale = 1.0f,
							  bool drawApRefOnAllStates = false,
							  bool skipAlign = false) const;

	virtual bool NeedDebugParenting() const;
	virtual const char* GetDebugParentingName() const;

	static void PrintEventDispatchStats();

protected:
	void DebugDrawApRef(const BoundFrame& apRef,
						const Locator& align,
						const char* apRefName,
						F32 scale,
						bool drawApReferenceParenting,
						bool drawNumericAlignAp,
						bool drawNumericAlignAndApCharterAndMaya) const;

public:

	void DrawAnimChannelLocators(StringId64 layerId = INVALID_STRING_ID_64,
								 float phase		= -1.0f,
								 bool drawLocators	= true,
								 bool printFloats	= true) const;
	U32F DrawAnimChannelLocators(const ArtItemAnim* pAnim, float phase, bool mirror) const;
	U32F DrawAnimChannelLocators(const AnimStateLayer* pBaseLayer, bool drawLocators, bool printFloats) const;
	virtual void GetUpdateFlagString(char flagChars[8]) const override;
	virtual void GetComment(char* pszComment, I32F nCommentSize) const override;

	Locator GetChannelLocatorLs(StringId64 locId, bool forceChannelBlending = false) const;

	virtual void ProcessRegionControlUpdate() override;

	virtual void PreRemoveMeshes();

	virtual void Show(int screenIndex = -1, bool secondary = false);
	virtual void Hide(int screenIndex = -1, bool secondary = false);

	virtual void ShowObject(int screenIndex = -1, bool secondary = false);
	virtual void HideObject(int screenIndex = -1, bool secondary = false);
	virtual void HideObjectForOneFrame();

	virtual void PhotoModeSetHidden(bool hidden);
	virtual bool GetPhotoModeHidden() const { return m_hiddenByPhotoMode; }
	/// --------------------------------------------------------------------------------------------------------------- ///
	/// High Contrast Mode
	/// --------------------------------------------------------------------------------------------------------------- ///
	void SetHighContrastMode(StringId64 hcmMode);
	void SetHighContrastMode(DC::HCMModeType hcmModeType);
	DC::HCMModeType GetHighContrastMode(bool ignoreInherited = false) const;
	const DC::SymbolArray* GetHighContrastModeMeshExcludeFilter() const { return m_pHcmMeshExcludeFilter.Valid() ? &(*m_pHcmMeshExcludeFilter) : nullptr; }
	void SetHighContrastModeMeshExcludeFilter(StringId64 meshExcludeFilterId);
	virtual void NotifyHighContrastModeOverriden() { m_highContrastModeOverridden = true; }
	virtual void SetHighContrastModeProxy(const NdGameObject* pProxy) {}
	virtual bool ManagesOwnHighContrastMode() const { return m_goFlags.m_managesOwnHighContrastMode; }
	void SetHighContrastModeForceUpdateFrame(I64 gameFrame) { m_forceUpdateHighContrastModeFrame = gameFrame; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	/// Camera
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual Locator GetLookAtAlignLocator() const { return GetLocator(); }

	virtual StringId64 GetStrafeCameraSettingsId() const { return SID("*camera-strafe-default*"); }
	float GetDistBlendCameraDist(const NdGameObject* pFocusObj) const;

	void SetCameraParent(NdGameObjectHandle hCameraParent) { m_hCameraParent = hCameraParent; }
	NdGameObjectHandle GetCameraParent() const;

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Poi
	/// --------------------------------------------------------------------------------------------------------------- ///

	static bool AddLookAtObject(const NdGameObject* pGo);
	static bool RemoveLookAtObject(const NdGameObject* pGo);
	static U32F GetLookAtObjectCount();
	static NdGameObjectHandle GetLookAtObject(U32F index);
	StringId64 GetPoiCollectType() const { return m_poiCollectType; }
	bool IsLookAtObject() const { return m_goFlags.m_isPoiLookAt; }
	float GetMinLookAtCollectRadius() const { return m_minPoiCollectRadius; }
	float GetMaxLookAtCollectRadius() const { return m_maxPoiCollectRadius; }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Generic Meshraycast. Every GameObject can do ground Meshraycaster.
	// You just need implement GetGroundMeshRaycastResult() function, and call KickGroundSurfaceTypeProbe at proper place.
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void KickGroundSurfaceTypeProbe(MeshRaycastType type);
	virtual Point GetGroundMeshRayPos(MeshRaycastType type) const { return GetCenterOfHeels(); }
	virtual Vector GetGroundMeshRayDir(MeshRaycastType type) const { return -GetLocalY(GetParentSpace().Rot()); }
	virtual float GetGroundMeshRayHalfLength(MeshRaycastType type) const { return 0.5f; }
	virtual MeshRaycastResult* GetSplasherMeshRaycastResult(DC::SplasherLimb splasherLimb) { return GetGroundMeshRaycastResult(kMeshRaycastGround); }	// Return any mesh raycasts already being done that correspond to the splasher body part
	virtual MeshRaycastResult* GetGroundMeshRaycastResult(MeshRaycastType type) { return nullptr; } // please override me!
	const MeshRaycastResult* GetGroundMeshRaycastResult(MeshRaycastType type) const { return const_cast<NdGameObject*>(this)->GetGroundMeshRaycastResult(type); }
	virtual Vector GetGroundWaterFlow();

	const MeshRaycastResult* GetGroundMeshRaycastResultConst(MeshRaycastType type) const {return const_cast<NdGameObject*>(this)->GetGroundMeshRaycastResult(type);}
	virtual void DebugDrawFootSurfaceType() const;

	Point GetCenterOfHeels() const;

	virtual const Pat GetCurrentPat() const { return Pat(0); }

	/// --------------------------------------------------------------------------------------------------------------- ///
	/// wheel meshraycaster. Now For DriveableVehicle and TurretTruck, but it can potentially work for all vehicles with wheels.
	// You just need implement ...
	/// --------------------------------------------------------------------------------------------------------------- ///
	void KickWheelMeshRaycast(I32 wheelIndex, Point_arg start, Point_arg end);
	virtual WheelMeshRaycastResult* GetWheelMeshRaycastResult(int wheelIndex) { return nullptr; } // please override me!

	virtual Vector GetEffectiveStickDir() const { return kZero; } //vehicles should override to give stick direction to use for camera, etc.

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Static Helper Functions
	/// --------------------------------------------------------------------------------------------------------------- ///
	static NdGameObject* LookupGameObjectByUniqueId(StringId64 uniqueId);
	static MutableNdGameObjectHandle LookupGameObjectHandleByUniqueId(StringId64 uniqueId);

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Data Members
	/// --------------------------------------------------------------------------------------------------------------- ///

public:
	static const U32		kUnspecifiedRandomizer = (U32)-1;

	GoFlags					m_goFlags;

protected:
	// Animation
	StringId64				m_resolvedLookId;
	StringId64				m_userIdUsedForLook;		//<! saved userId since the id could be changed after the look was resolved (used by child props)
	U32						m_lookCollectionRandomizer;
	U32						m_lookTintRandomizer;
	StringId64				m_resolvedAmbientOccludersId;
	Color*					m_aResolvedLookTint;
	mutable SkeletonId		m_skeletonId;
	IDrawControl*			m_pDrawControl;				//<! ptr to draw control or NULL (relocate)
	AnimControl*			m_pAnimControl;				//<! ptr to anim control or NULL (relocate)
	FgAnimData*				m_pAnimData;				//<! ptr to anim data or NULL
	IEffectControl*			m_pEffectControl;			//<! ptr to effect control or NULL (relocate)

	TimeFrame				m_firstRequestSoundTime;
	float				    m_soundPhysFxDelayTime;
	TimeFrame				m_lastDestructionFxSoundTime;
	U32						m_numDestructionFxSoundsThisFrame;

	// Attach System
	AttachSystem*			m_pAttachSystem;

	// Targetable
	Targetable*				m_pTargetable;				//<! ptr to targetable if we have one or NULL (relocate)

	// Faction
	FactionId				m_faction;					//<! faction of this object

	// Splashers
	SplasherController*			m_pSplasherController;
	StringId64					m_splasherSkeletonId;
	StringId64					m_splasherSetlistId;
	static ndjob::CounterHandle	s_pSplasherJobCounter;

	// State Script
	SsInstance*					m_pScriptInst;	//<! ptr to script instance (relocate)
	StringId64*					m_aChildScriptEventId;
	I32							m_childScriptEventCapacity;
	I32							m_childScriptEventCount;

	// IGC Animation
	StringId64					m_playingIgcName;
	StringId64					m_playingIgcAnim;
	F32							m_playingIgcDurationSec;
	F32							m_playingIgcStartTimeSec;

	F32							m_initialAmbientScale;

	// Physics
	mutable U32					m_havokSystemId;
	CompositeBody*				m_pCompositeBody;			//<! protected to permit derived classes to init in a custom way if desired (rare)
	Point						m_prevPosWs;
	Point						m_prevPosPs;
	Vector						m_velocityWs;
	Vector						m_velocityPs;
	Point						m_prevCheckPointPosWs;
	Vector						m_angularVecWs;

	Point						m_prevRegionControlPos;

	// Nav
	DynamicNavBlocker*			m_pDynNavBlocker;			//<! ptr to nav blocker or NULL (external no-relocate)
	StaticNavBlocker*			m_pStaticNavBlocker;		//<! ptr to nav blocker or NULL (external no-relocate)
	I32							m_dynNavBlockerJointIndex;
	NavPolyHandle				m_hBlockedNavPoly;			//<! blocked nav mesh polygon or NULL (no relocate)
	PlatformControl*			m_pPlatformControl;			//<! ptr to platform control or NULL (relocate)

	SimpleGridHashId			m_gridHashId;

	// Spline Following
	static const F32			kInvalidSplineNotifyDistance;
	F32							m_splineNotifyDistance_m;
	SsAction					m_ssAtSplineDistanceAction;
	CatmullRom::Handle			m_hNotifySpline;
	SplineTracker*				m_pSplineTracker;			//<! if attached to a valid spline, game object will follow that spline automatically (relocate)

	// Region
	RegionControl*				m_pRegionControl;			//<! ptr to region control or NULL (relocate)
	StringId64					m_charterRegionActorId;		//<! special region actor id set from Charter

	// Attack Handling
	NdAttackHandler*			m_pAttackHandler;

	// Network
	NdNetController*			m_pNetController;

#ifdef HEADLESS_BUILD
	NdNetLodController*			m_pNetLodController;
#endif

	// Sound
	NdFoliageSfxControllerBase*	m_pFoliageSfxController;
	VbHandle*					m_paSoundBankHandles;			//<! pointer to array of sound bank handles used by look system, if object has any sound banks
	StringId64					m_gasMaskFutz;

	// Flags
	bool						m_bOnIsValidBindTargetCalled : 1;	//<! OnIsValidBindTarget() has been called
	bool						m_bDisableFeatureDbs : 1;
	bool						m_bEnableFeatureDbsForAnyLayer : 1;
	mutable bool				m_bAllocatedHavokSystemId : 1;
	bool						m_bFreezeBindingWhenNonPhysical : 1;	//<! By default we update non-physical bodies to allow parent bindings to them; enable this flag to freeze a non-physical binding to where it was when it was last game-driven.
	bool						m_wantNavBlocker : 1;
	bool						m_bUpdateCoverActionPacks : 1;	//<! If set then update cover action packs
	bool						m_feetSplashersInWater : 1;
	bool						m_feetWet: 1;
	bool						m_spawnShaderInstanceParamsSet : 1;
	bool						m_hasJointWind : 1;
	bool						m_hiddenByPhotoMode : 1;
	bool						m_highContrastModeOverridden : 1;
private:
	bool						m_ignoredByNpcs : 1;	// NPCs don't attack or interact with
	TimeFrame					m_lastIgnoredByNpcsTime;
protected:

	ProbeTreeWeightControl*		m_pProbeTreeWeightControl;		//<! for controling weights for individual probe trees

	TimeFrame					m_spawnTime;

	// shimmer
	ScriptPointer<DC::ShimmerSettings>	m_pShimmerSettings;
	bool										m_updateShimmer;
	F32											m_shimmerIntensity;
	F32											m_shimmerAlphaMultiplier;
	F32											m_shimmerOffset;
	F32											m_shimmerIntensityBlendTarget;
	F32											m_shimmerIntensityBlendTime;

	// Instance Textures
	static const I32			kMaxInstanceTextureTables = 3;
	InstanceTextureTable*		m_pInstanceTextureTable[kMaxInstanceTextureTables];
	I8							m_numInstanceTextureTables;

	I8							m_wetMaskIndexes[kMaxInstanceTextureTables];

	// Joint-Shader driving
	ScriptPointer<DC::JointShaderDriverList> m_pJointShaderDriverList;

	// Attach background to this foreground object
	BgAttacher*					m_pBgAttacher;

	// Optional module to handle joint overrides
	JointOverrideData*			m_pJointOverrideData;
	EvaluateJointOverridePluginData* m_pJointOverridePluginCmd;
	const DC::Map*				m_pMaterialSwapMapToApply;

	//Material remap
	U8							m_numInstanceMaterialRemaps;
	InstanceMaterialRemap*		m_aInstanceMaterialRemaps;

	// Stores locator from previous frame
	// Currently only used and only updates for the fastAnimUpdate optimized objects
	Locator						m_prevLocator;

	// Did anything moved in last FastAnimDataUpdate?
	bool						m_fastAnimUpdateMoved;

	// Debug Info
	LookDebugInfo*				m_pDebugInfo;

	I32							m_windMgrIndex;

	// spec cubemaps
	I16							m_cubemapIndex;
	I16							m_cubemapBgIndex;
	StringId64					m_cubemapBgNameId;
	SpecularIblProbeParameters* m_pSpecularIblProbeParameters;

	// POI
	float						m_minPoiCollectRadius;
	float						m_maxPoiCollectRadius;
	StringId64					m_poiCollectType;

	// Interactable
	NdInteractControl*			m_pInteractCtrl;

	// attachment
	AttachmentCtrl*				m_pAttachmentCtrl;

	ScriptPointer<const DC::SymbolArray> m_pHcmMeshExcludeFilter;
	I64							m_forceUpdateHighContrastModeFrame;

	I64							m_groundSurfaceTypeKickedFN[kMeshRaycastCount];

private:

	/// --------------------------------------------------------------------------------------------------------------- ///
	/// Camera
	/// --------------------------------------------------------------------------------------------------------------- ///
	NdGameObjectHandle			m_hCameraParent;

protected:
	/// --------------------------------------------------------------------------------------------------------------- ///
	// Implementation
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void ProcessUpdateInternal();
	virtual U32F GetMaxStateAllocSize() override;
	virtual FgAnimData* AllocateAnimData(const ArtItemSkeletonHandle artItemSkelHandle);
	virtual Err ConfigureAnimData(FgAnimData* pAnimData, const ResolvedBoundingData& bounds);
	virtual Err InitAnimControl(const ProcessSpawnInfo& spawn);
	virtual U32 GetEffectListSize() const;
	virtual Err SetupAnimControl(AnimControl* pAnimControl);
	virtual Err InitializeDrawControl(const ProcessSpawnInfo& spawn, const ResolvedLook& resolvedLook);
	virtual void SetupMeshAudioConfigForLook(const ResolvedLook& resolvedLook);
	virtual void OnRemoveParentRigidBody(RigidBody* pOldParentBody, const RigidBody* pNewParentBody) override;
	virtual void OnAddParentRigidBody(const RigidBody* pOldParentBody, Locator oldParentSpace, const RigidBody* pNewParentBody) override;
	virtual bool CanTrackSplines(const SpawnInfo& spawn) const;
	void CheckIsValidBindTarget();
	virtual void OnLookResolved(const ResolvedLook* pResolvedLook) {}
	virtual void AddAmbientOccludersToDrawControl(StringId64 occluderListId, StringId64 alternateOccluderListId = INVALID_STRING_ID_64);
	virtual Locator GetAmbientOccludersLocator() const;
	void UpdateAmbientOccluders();
	StringId64 GetAmbientOccludersId() const { return m_resolvedAmbientOccludersId; }
	virtual bool EnableEffectFilteringByDefault() const { return false; }
	virtual float SoundEffectFilteringMinTime() const { return -1.0f; }
	virtual bool NeedsEffectControl(const ProcessSpawnInfo& spawn) const { return true; }
	virtual bool NeedsAttachSystem() const { return true; }
	virtual void NotifyMissingShaderInstParamsTag() const;
	virtual StringId64 GetForceStateScriptId() const { return INVALID_STRING_ID_64; }
	virtual bool IsStateScriptDisabled(const ProcessSpawnInfo& spawn) const { return false; }

	void RegisterInListenModeMgrImpl(NdGameObject* pParent, U32 addFlags = 0, float fadeTime = 0.0f);
	void UnregisterFromListenModeMgrImpl(float fadeTime);

	void DoFrameSetupOfJointOverridePlugin();

	void SetShimmerIntensityInternal(F32 val);
	virtual void UpdateHighContrastModeForFactionChange();

	static const U32 kMaxNumDeferredMaterialSwaps = 10;
	static ProcessHandle sm_deferredMaterialSwapObjects[kMaxNumDeferredMaterialSwaps];
	static U32 sm_numDeferredMaterialSwaps;

	static const U32 kMaxLookAtObjects = 256;
	static NdGameObjectHandle s_lookAtObjects[kMaxLookAtObjects];
	static U32 s_lookAtObjectCount;
	static NdAtomic64 s_lookAtObjectLock;
private:
	void DebugDrawPickups();
	virtual bool ShouldDebugDrawJoint(const DebugDrawFilter& filter, int globalJointIndex) const;
};

PROCESS_DECLARE(NdGameObject);

/// --------------------------------------------------------------------------------------------------------------- ///
// NdGameObject::Active State
// Derive all states from this class for automatic update of the state script instance (or make
// sure to call the script instance's ScriptUpdate() function from your ScriptUpdate()).
/// --------------------------------------------------------------------------------------------------------------- ///
class NdGameObject::Active : public Process::State
{
public:
	BIND_TO_PROCESS(NdGameObject);
	virtual ~Active() override {}
	virtual void Enter() override;
	virtual void Update() override;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NdGameObjectSnapshot : public NdLocatableSnapshot
{
public:
	PROCESS_SNAPSHOT_DECLARE(NdGameObjectSnapshot, NdLocatableSnapshot);

	explicit NdGameObjectSnapshot(const Process* pOwner, const StringId64 typeId)
		: ParentClass(pOwner, typeId)
		, m_boundingSphere(kZero)
		, m_lookId(INVALID_STRING_ID_64)
		, m_velocityWs(kZero)
		, m_velocityPs(kZero)
		, m_factionId(0)
		, m_skelNameId(INVALID_STRING_ID_64)
		, m_pDynNavBlocker(nullptr)
		, m_minPoiCollectRadius(0.0f)
		, m_maxPoiCollectRadius(0.0f)
		, m_pAnimData(nullptr)
		, m_hasAnimControl(false)
		, m_hasTargetable(false)
		, m_feetWet(false)
	{
	}

	virtual Err Init(const Process* pOwner) override;

	StringId64 GetSkelNameId() const		{ return m_skelNameId; }
	FactionId GetFactionId() const			{ return m_factionId; }
	Vector GetVelocityWs() const			{ return m_velocityWs; }
	Vector GetVelocityPs() const			{ return m_velocityPs; }
	const FgAnimData* GetAnimData() const	{ return m_pAnimData; }
	bool HasAnimControl() const				{ return m_hasAnimControl; }

	const DynamicNavBlocker* GetNavBlocker() const { return m_pDynNavBlocker; }
	const NetPlayerTracker* GetNetOwnerTracker() const { return m_pNetPlayerTracker; }

	const StringId64 GetLookId() const { return m_lookId; }

	Sphere GetBoundingSphere() const { return m_boundingSphere; }

	bool AreFeetWet() const { return m_feetWet; }
	bool IgnoredByNpcs() const { return m_ignoredByNpcs; }

	Sphere						m_boundingSphere;
	StringId64					m_lookId;
	Vector						m_velocityWs;
	Vector						m_velocityPs;
	FactionId					m_factionId;
	StringId64					m_skelNameId;
	const DynamicNavBlocker*	m_pDynNavBlocker;
	NdGameObject::GoFlags		m_goFlags;
	float						m_minPoiCollectRadius;
	float						m_maxPoiCollectRadius;
	StringId64					m_poiCollectType;
	const FgAnimData*			m_pAnimData;
	const NetPlayerTracker*		m_pNetPlayerTracker;
	bool						m_hasAnimControl : 1;
	bool						m_hasTargetable : 1;
	bool						m_feetWet : 1;
	bool						m_ignoredByNpcs : 1;
};

/// --------------------------------------------------------------------------------------------------------------- ///
extern NdGameObject::DebugDrawFilter g_gameObjectDrawFilter;

/// --------------------------------------------------------------------------------------------------------------- ///
void GetBindingString(const RigidBody* pBody, const StringId64 bindSpawnerId, char* text, const U32 textSize);
