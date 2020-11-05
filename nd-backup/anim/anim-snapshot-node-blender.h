/*
 * Copyright (c) 2014 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ANIM_SNAPSHOT_NODE_BLENDER_H
#define ANIM_SNAPSHOT_NODE_BLENDER_H

#include "ndlib/anim/anim-snapshot-node-blend.h"

template <typename DATA_TYPE>
class AnimSnapshotNodeBlender
{
public:
	virtual ~AnimSnapshotNodeBlender() {}

	DATA_TYPE Blend(const AnimStateSnapshot* pSnapshot, const AnimSnapshotNode* pStartingNode, DATA_TYPE initialData);

protected:
	virtual DATA_TYPE GetDefaultData() const = 0;
	virtual bool GetDataForNode(const AnimStateSnapshot* pSnapshot, const AnimSnapshotNode* pNode, DATA_TYPE* pDataOut) = 0;
	virtual DATA_TYPE BlendData(const DATA_TYPE& leftData, const DATA_TYPE& rightData, float fade) = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename DATA_TYPE>
DATA_TYPE AnimSnapshotNodeBlender<DATA_TYPE>::Blend(const AnimStateSnapshot* pSnapshot, const AnimSnapshotNode* pStartingNode, DATA_TYPE initialData)
{
	if (!pStartingNode)
		return initialData;

	DATA_TYPE nodeData = initialData;

	if (const AnimSnapshotNodeBlend* pBlendNode = AnimSnapshotNodeBlend::FromAnimNode(pStartingNode))
	{
		const AnimSnapshotNode* pLeftNode = pSnapshot->GetSnapshotNode(pBlendNode->m_leftIndex);
		const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(pBlendNode->m_rightIndex);

		const DATA_TYPE leftData = Blend(pSnapshot, pLeftNode, initialData);
		const DATA_TYPE rightData = Blend(pSnapshot, pRightNode, initialData);
		
		nodeData = BlendData(leftData, rightData, pBlendNode->m_blendFactor);
	}
	else if (!GetDataForNode(pSnapshot, pStartingNode, &nodeData))
	{
		nodeData = GetDefaultData();
	}

	return nodeData;
}

#endif // ANIM_SNAPSHOT_NODE_BLENDER_H
