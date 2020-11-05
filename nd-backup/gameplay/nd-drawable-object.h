/*
* Copyright (c) 2004 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/nd-weapon-upgrade.h"

#include "gamelib/state-script/ss-animate.h"

class EffectAnimInfo;
class EffectControlSpawnInfo;
class NdPropControl;
class ProcessSpawnInfo;
class ResolvedLook;
class SpawnInfo;
class SsAnimateController;

namespace DC
{
};

FWD_DECL_PROCESS_HANDLE(NdDrawableObject);

/// --------------------------------------------------------------------------------------------------------------- ///
class NdDrawableObject : public NdGameObject
{
	typedef NdGameObject ParentClass;

public:
	STATE_DECLARE_OVERRIDE(Active);
	FROM_PROCESS_DECLARE(NdDrawableObject);

	NdDrawableObject();
	virtual ~NdDrawableObject() override;
	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual Err PostInit(const ProcessSpawnInfo& spawn) override;
	virtual Err InitializeDrawControl(const ProcessSpawnInfo& spawn, const ResolvedLook& resolvedLook) override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void OnKillProcess() override;
	virtual int GetChildScriptEventCapacity() const override { return 8; }
	virtual SsAnimateController* GetPrimarySsAnimateController() const override;
	virtual void PreUpdate() override;
	virtual void PostAnimUpdate_Async() override;
	virtual void DebugShowProcess(ScreenSpaceTextPrinter& printer) const override;
	virtual void PopulateEffectControlSpawnInfo(const EffectAnimInfo* pEffectAnimInfo,
												EffectControlSpawnInfo& effInfo) override;
	virtual void EventHandler(Event& event) override;
	virtual ProcessSnapshot* AllocateSnapshot() const override;
	virtual void RefreshSnapshot(ProcessSnapshot* pSnapshot) const override;
	virtual void NotifyDebugSelected() override;

	virtual StringId64 GetLookId() const override;

	SsAnimateController* GetAnimateCtrl(int index) const;
	virtual int GetNumAnimateLayers() const;
	virtual int GetNumAllocateLayers() const;
	bool IsSimpleAnimating() const override;

	virtual NdPropControl* GetNdPropControl() override { return m_pPropControl; }
	virtual const NdPropControl* GetNdPropControl() const override { return m_pPropControl; }

	virtual MutableNdGameObjectHandle GetRenderTargetOwner() const { return MutableNdGameObjectHandle(nullptr); }

	virtual float GetAnimationPhaseDirect(I32 layer = 0, StringId64 animId = INVALID_STRING_ID_64) override;

protected:
	virtual NdPropControl* CreatePropControl(const SpawnInfo& spawn, const ResolvedLook& resolvedLook);

	virtual SsAnimateController* CreateAnimateCtrl(StringId64 layerName, bool additive);

	void HideJoint(JointModifiers* pJointModifiers, StringId64 jointId);

	void ApplyVisualMods(
		JointModifiers* pJointModifiers,
		const ListArray<WeaponUpgrades::JointMod>& jointMods,
		const ListArray<WeaponUpgrades::ShaderParams>& shaderParams);

	virtual bool HandlePlayAnimEvent(Event& event);

	SsAnimateController**						m_pSsAnimateCtrl;
	U32											m_numAnimateLayers;

	MeshProbe::SurfaceType						m_effSurfaceType; // basic surface-type used by EFF. Inherited class might override it.

	StringId64									m_lookId;

	U32											m_extraPropCapacity;
	NdPropControl*								m_pPropControl;

	bool										m_suppressSsAnimatePostAnimUpdate;
};


/// --------------------------------------------------------------------------------------------------------------- ///
class NdDrawableObject::Active : public NdGameObject::Active
{
private:
	typedef NdGameObject::Active ParentClass;
public:
	BIND_TO_PROCESS(NdDrawableObject);

	virtual ~Active() override {}
	virtual void Update() override;
};

/// --------------------------------------------------------------------------------------------------------------- ///
// NdDrawableSsAnimateEventContext - For having NdDrawableObject interface
//		with SSAnimate and SSAnimateSimple
/// --------------------------------------------------------------------------------------------------------------- ///
class NdDrawableSsAnimateEventContext : public ISsAnimateEventContext
{
public:
	NdDrawableSsAnimateEventContext(NdDrawableObject* pObject) : m_pObject(pObject) {}

	virtual NdGameObject* GetOwner() const override
	{
		return m_pObject;
	}

	virtual void GetControllerIndexRanges(ControllerIndexRanges& ranges) const override
	{
		ranges.iScriptFullBodyController = 0;

		ranges.iBeginScriptGestureControllers = 1;
		ranges.iEndScriptGestureControllers = m_pObject->GetNumAnimateLayers();

		ranges.iBeginScriptControllers = 0;
		ranges.iEndScriptControllers = m_pObject->GetNumAnimateLayers();
	}

	virtual SsAnimateController* GetController(I32F i) const override
	{
		ALWAYS_ASSERT(m_pObject);
		ALWAYS_ASSERT(i >= 0 && i < m_pObject->GetNumAnimateLayers());
		return m_pObject->GetAnimateCtrl(i);
	}

	virtual SsAnimateController* GetControllerByLayerName(StringId64 layerName) const override
	{
		const int numLayers = m_pObject ? m_pObject->GetNumAnimateLayers() : 0;
		
		for (int i = 0; i < numLayers; ++i)
		{
			SsAnimateController* pController = m_pObject->GetAnimateCtrl(i);
			if (pController && (pController->GetLayerId() == layerName))
			{
				return pController;
			}
		}

		return nullptr;
	}

	NdDrawableObject* m_pObject;
};
