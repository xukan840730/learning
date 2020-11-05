/*
* Copyright (c) 2004 Naughty Dog, Inc. 
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "gamelib/gameplay/nd-attachment-ctrl.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/nd-weapon-upgrade.h"

class EffectAnimInfo;
class EffectControlSpawnInfo;
class NdPropControl;
class ProcessSpawnInfo;
class ResolvedLook;
class SpawnInfo;
class SsAnimateController;
struct LightUpdateData;

class NdLightObject : public NdGameObject
{
	typedef NdGameObject ParentClass;

public:
	STATE_DECLARE_OVERRIDE(Active);
	FROM_PROCESS_DECLARE(NdLightObject);

	NdLightObject();
	virtual ~NdLightObject() override;
	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void OnKillProcess() override;
	virtual SsAnimateController* GetPrimarySsAnimateController() const override;
	virtual void PreUpdate() override;
	virtual void PostJointUpdate_Async() override;
	virtual void PostAnimUpdate_Async() override;
	virtual void PostAnimBlending_Async() override;
	virtual void DebugShowProcess(ScreenSpaceTextPrinter& printer) const override;
	virtual void EventHandler(Event& event) override;
	virtual ProcessSnapshot* AllocateSnapshot() const override;
	virtual void RefreshSnapshot(ProcessSnapshot* pSnapshot) const override;
	virtual void ResolveLook(ResolvedLook& resolvedLook,
							 const SpawnInfo& spawn,
							 StringId64 desiredActorOrLookId) override;
	virtual void SetLightTable(LightTable* pLightTable) override { m_pLightTable = pLightTable; }
	virtual void SetTweakedLightTable(LightTable* pTweakedLightTable) override
	{
		m_pTweakedLightTable = pTweakedLightTable;
	}
	virtual void SetAnimatedLights(const AnimatedLight* pAnimatedLights) override
	{
		m_pAnimatedLights = pAnimatedLights;
	}
	virtual const Locator GetRealJointLocatorWs(const JointCache* pJointCache, U32 jointIndex) const
	{
		return pJointCache->GetJointLocatorWs(jointIndex);
	}
	virtual const StringId64 GetLightTableActorNameId() const;
	const AnimatedLight* GetAnimatedLight(U32 index) const { return m_pAnimatedLights + index; }
	virtual void AddLightTable() const;
	virtual void RemoveLightTable() const;
	virtual void MatchLightInTable(LightType lightType, I32 lightIndex){};
	virtual bool IsShadowCastingSpotLight() const { return false; }
	virtual void SetIntensity(float intensity) {}

	virtual StringId64 GetLookId() const override;

	SsAnimateController* GetAnimateCtrl(int index) const;
	int GetNumAnimateLayers() const;
	virtual int GetNumAllocateLayers() const;
	virtual FgAnimData* AllocateAnimData(const ArtItemSkeletonHandle artItemSkelHandle) override;
	bool IsSimpleAnimating() const override;

	bool AttachToObject(StringId64 parentObjectId, StringId64 attachOrJointId, bool manual = false);
	void Detach(bool manual = false);
	StringId64 GetAttachmentObjectId() const;
	StringId64 GetAttachmentAttachOrJointId() const;
	bool IsAttachmentManagedManually() const { return m_attachmentManagedManually; }
	void ResetTweakedLights();

	static bool UpdateMayaLight(const LightUpdateData& data)
	{
		UpdateMayaLightIterator iter(data);
		return iter.m_updatedLight;
	}

	static const char* DetermineLightName(const BaseLight* pLight);

protected:
	virtual SsAnimateController* CreateAnimateCtrl(StringId64 layerName, bool additive);
	virtual bool UpdateMayaLightInternal(const LightUpdateData& data);
	virtual void PreEventHandler(Event& event) {}
	virtual void PostEventHandler(Event& event);
	virtual U8 FindLightTableIndex(const Level* pActor) const;
	virtual U8 FindAnimatedLightIndex(const Level* pActor, U8 defaultIndex) const;
	virtual bool LiveUpdateMayaLight(const LightUpdateData& data);
	Vec4 GetEnvironmentColorAndIntensityMultipliers() const;
	friend class DetermineLightNameIterator;
	const char* DetermineLightNameInternal(const BaseLight* pLight);

	SsAnimateController**		m_pSsAnimateCtrl;
	U32							m_numAnimateLayers;
	
	StringId64					m_lookId;	

	bool						m_attachmentManagedManually;
	bool						m_suppressSsAnimatePostAnimUpdate;
	const AnimatedLight*		m_pAnimatedLights;
	LightTable*					m_pLightTable;
	LightTable*					m_pTweakedLightTable;
	bool						m_resetTweakedLights;

private:
	virtual bool ShouldDebugDrawJoint(const DebugDrawFilter& filter, int globalJointIndex) const override;

	class UpdateMayaLightIterator
	{
	private:
		const LightUpdateData& m_data;

		static bool ProcessVisitor(Process* pProc, void* pUserData)
		{
			if (pProc->IsKindOf(SID("NdLightObject")))
			{
				UpdateMayaLightIterator* pIter = PunPtr<UpdateMayaLightIterator*>(pUserData);
				NdLightObject* pLightObject = PunPtr<NdLightObject*>(pProc);
				pIter->m_updatedLight |= pLightObject->UpdateMayaLightInternal(pIter->m_data);
				return !pIter->m_updatedLight;	// stop walking the tree once one NdLightObject is updated
			}

			return true;
		}

	public:
		explicit UpdateMayaLightIterator(const LightUpdateData& data);

		bool m_updatedLight;
	};

	int m_colorROffset;
	int m_colorGOffset;
	int m_colorBOffset;
	int m_coneAngleOffset;
	int m_penumbraAngleOffset;
	int m_shadowBlurScaleOffset;
	int m_shapeRadiusOffset;
	int m_volIntensityOffset;
	int m_shadowSlopeOffset;
	int m_shadowConstOffset;
	int m_nearClipDistOffset;
	int m_farClipDistOffset;
	int m_useRayTraceShadowsOffset;
	int m_decayRateOffset;
	int m_startDistanceOffset;
	int m_startRangeOffset;
	int m_radiusOffset;
	int m_skipBgShadowOffset;
	int m_emitDiffuseOffset;
	int m_emitSpecularOffset;
	int m_noLightBgOffset;
	int m_noLightFgOffset;
	int m_castsSssOffset;
	int m_enableBounceOffset;
	int m_minRoughnessOffset;
	int m_charOnlyOffset;
	int m_eyesOnlyOffset;
	int m_hairOnlyOffset;
	int m_particleOnlyOffset;
	int m_particleOffset;
	int m_propOnlyOffset;
	int m_noHairOffset;
	int m_noTransmittanceOffset;
	int m_specScaleOffset;
	int m_noLightCharOffset;
	int m_projWidthOffset;
	int	m_projHeightOffset;
	int	m_goboUScrollSpeedOffset;
	int	m_goboVScrollSpeedOffset;
	int	m_goboUTileFactorOffset;
	int	m_goboVTileFactorOffset;
	int	m_goboAnimSpeedOffset;
	int	m_volInnerFalloffOffset;
	int	m_volInnerIntensityOffset;
	int	m_minShadowBlurScaleOffset;
	int	m_maxShadowBlurDistOffset;
	int	m_variableShadowBlurOffset;
};

PROCESS_DECLARE(NdLightObject);

class NdLightObject::Active : public NdGameObject::Active
{
private:
	typedef NdGameObject::Active ParentClass;
public:
	BIND_TO_PROCESS(NdLightObject);

	virtual ~Active() override {}
	virtual void Update() override;
};
