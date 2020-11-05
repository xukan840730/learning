/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ANIM_STATE_INSTANCE_LIST_H
#define ANIM_STATE_INSTANCE_LIST_H

#include "ndlib/anim/anim-state-layer.h"
#include "corelib/containers/list-array.h"

class AnimStateInstanceList
{
public:
	static void GetAnimStateInstanceList(const AnimStateLayer* pLayer, ListArray<const AnimStateInstance*>& list)
	{
		InstanceVisitor visitor(&list);
		visitor.BlendForward(pLayer, 0);
	}

private:

	typedef I32 VisitorDataType;
	class InstanceVisitor : public AnimStateLayer::InstanceBlender<VisitorDataType>
	{
		ListArray<const AnimStateInstance*>* m_pInstanceList;
	public:
		InstanceVisitor(ListArray<const AnimStateInstance*>* pInstanceList)
			: m_pInstanceList(pInstanceList)
		{
			m_pInstanceList->Clear();
		}

		virtual VisitorDataType GetDefaultData() const override { return 0;}
		virtual bool GetDataForInstance(const AnimStateInstance* pInstance, VisitorDataType* pDataOut) override
		{
			m_pInstanceList->PushBack(pInstance);
			*pDataOut = 0;
			return true;
		}
		virtual VisitorDataType BlendData(const VisitorDataType& leftData, const VisitorDataType& rightData, float masterFade, float animFade, float motionFade) override
		{
			return 0;
		}
	};
};

#endif //ANIM_STATE_INSTANCE_LIST_H