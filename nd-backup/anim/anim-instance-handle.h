/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ANIM_INST_HANDLE_H
#define ANIM_INST_HANDLE_H

#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-simple-instance.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-control.h"

#include "gamelib/gameplay/nd-game-object.h"

class AnimInstanceHandle
{	
public:	
	struct AnimInstanceStateRef
	{
		const NdGameObject*			m_pGameObj;
		const AnimStateLayer*		m_pStateLayer;
		const AnimStateInstance*	m_pStateInst;

		AnimInstanceStateRef()
			: m_pGameObj(nullptr)
			, m_pStateLayer(nullptr)
			, m_pStateInst(nullptr)
		{}
	};

	struct AnimInstanceSimpleRef
	{
		const NdGameObject*			m_pGameObj;
		const AnimSimpleLayer*		m_pSimpleLayer;
		const AnimSimpleInstance*	m_pSimpleInst;

		AnimInstanceSimpleRef()
			: m_pGameObj(nullptr)
			, m_pSimpleLayer(nullptr)
			, m_pSimpleInst(nullptr)
		{}
	};

	AnimInstanceHandle()
	{
		InitIds();
		m_type = kHandleTypeInvalid;
		m_sortByTopInstance = true;
	}

	AnimInstanceHandle(const NdGameObject* pGameObj, const AnimLayer* pLayer, const AnimInstance* pInst, bool sortByTopInstance = true)
	{
		InitIds();
		m_hGameObject = pGameObj;
		m_layerId = pLayer ? pLayer->GetName() : INVALID_STRING_ID_64;
		m_instanceId = pInst ? pInst->GetId() : INVALID_ANIM_INSTANCE_ID;
		if (pInst)
			m_type = (pInst->IsSimple()) ? kHandleTypeSimple : kHandleTypeState;
		else
			m_type = kHandleTypeInvalid;
		m_sortByTopInstance = sortByTopInstance;
		ANIM_ASSERT(!pLayer || !pInst || (pLayer->GetType() == kAnimLayerTypeSimple && pInst->IsSimple()) || (pLayer->GetType() == kAnimLayerTypeState && !pInst->IsSimple()));
	}

	StringId64 LayerId() const { return m_layerId; }

	bool operator ==(const AnimInstanceHandle& rhs) const
	{
		return m_hGameObject == rhs.m_hGameObject 
			&& m_layerId == rhs.m_layerId 
			&& m_type == rhs.m_type
			&& m_instanceId == rhs.m_instanceId;
	}

	AnimInstanceStateRef GetStateInstanceData() const
	{
		AnimInstanceStateRef result;
		if (m_type == kHandleTypeState)
		{
			if (const NdGameObject* pGameObj = m_hGameObject.ToProcess())
			{
				if (const AnimControl* pAnimControl = pGameObj->GetAnimControl())
				{
					if (const AnimStateLayer* pStateLayer = pAnimControl->GetStateLayerById(m_layerId))
					{
						result.m_pGameObj = pGameObj;
						result.m_pStateLayer = pStateLayer;
						result.m_pStateInst = pStateLayer->GetInstanceById(m_instanceId);						
					}
				}
			}
		}
		return result;
	}

	const AnimStateInstance* GetStateInstance() const
	{
		return GetStateInstanceData().m_pStateInst;
	}

	AnimInstanceSimpleRef GetSimpleInstanceRef() const
	{
		AnimInstanceSimpleRef result;
		if (m_type == kHandleTypeSimple)
		{
			if (const NdGameObject* pGameObj = m_hGameObject.ToProcess())
			{
				if (const AnimControl* pAnimControl = pGameObj->GetAnimControl())
				{
					if (const AnimSimpleLayer* pSimpleLayer = pAnimControl->GetSimpleLayerById(m_layerId))
					{
						result.m_pGameObj = pGameObj;
						result.m_pSimpleLayer = pSimpleLayer;
						result.m_pSimpleInst = pSimpleLayer->GetInstanceById(m_instanceId);						
					}
				}
			}
		}
		return result;
	}

	const AnimSimpleInstance* GetSimpleInstance() const
	{
		return GetSimpleInstanceRef().m_pSimpleInst;
	}

	const NdGameObject* GetGameObject() const { return m_hGameObject.ToProcess(); }
	NdGameObjectHandle GetGameObjectHandle() const { return m_hGameObject; }

	bool IsTopInstance() const 
	{
		if (m_sortByTopInstance)
		{
			if (m_type == kHandleTypeSimple)
			{
				AnimInstanceSimpleRef simpleInfo = GetSimpleInstanceRef();
				return (simpleInfo.m_pSimpleInst != nullptr && simpleInfo.m_pSimpleInst == simpleInfo.m_pSimpleLayer->GetInstance(0));
			}
			else if (m_type == kHandleTypeState)
			{
				AnimInstanceStateRef stateInfo = GetStateInstanceData();
				return (stateInfo.m_pStateInst != nullptr && stateInfo.m_pStateInst == stateInfo.m_pStateLayer->CurrentStateInstance());
			}
		}
		return false;
	}
	

private:
	enum EHandleType { kHandleTypeInvalid, kHandleTypeState, kHandleTypeSimple};

	void InitIds()
	{
		m_instanceId = INVALID_ANIM_INSTANCE_ID;
	}

	NdGameObjectHandle m_hGameObject;
	StringId64				m_layerId;
	AnimInstance::ID		m_instanceId;
	EHandleType				m_type;	
	bool					m_sortByTopInstance;
};

#endif //ANIM_INST_HANDLE_H
