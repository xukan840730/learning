/*
 * Copyright (c) 2012 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-snapshot-node-blend.h"
#include "ndlib/anim/anim-state-snapshot.h"

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename DATA_TYPE>
class EvaluateAnimTree
{
	I32 m_recusionCount;

public:
	EvaluateAnimTree()
		: m_recusionCount(0)
	{}

	virtual ~EvaluateAnimTree() { }

	DATA_TYPE Evaluate(const AnimStateSnapshot* pStateSnapshot)
	{
		return EvaluateRecursive(pStateSnapshot, pStateSnapshot->m_rootNodeIndex);
	}	

protected:
	virtual DATA_TYPE GetDefaultData() const = 0;
	virtual bool GetDataFromNode(const AnimSnapshotNode* pNode, DATA_TYPE* pDataOut) = 0;
	virtual DATA_TYPE BlendData(const DATA_TYPE& leftData,
								const DATA_TYPE& rightData, 
								const AnimSnapshotNodeBlend* pBlendNode, 
								bool flipped) = 0;

private:
	DATA_TYPE EvaluateRecursive(const AnimStateSnapshot* pStateSnapshot, U32F nodeIndex)
	{
		const AnimSnapshotNode* pNode = pStateSnapshot->GetSnapshotNode(nodeIndex);
		m_recusionCount++;

		if (const AnimSnapshotNodeBlend* pBlendNode = AnimSnapshotNodeBlend::FromAnimNode(pNode))
		{
			//See if we really want to blend
			if (pBlendNode->m_flags & DC::kAnimNodeBlendFlagEvaluateLeftNodeOnly)
			{
				return EvaluateRecursive(pStateSnapshot, pBlendNode->m_leftIndex);
			}
			else
			{			
				DATA_TYPE leftData = EvaluateRecursive(pStateSnapshot, pBlendNode->m_leftIndex);
				DATA_TYPE rightData = EvaluateRecursive(pStateSnapshot, pBlendNode->m_rightIndex);
				return BlendData(leftData, rightData, pBlendNode, pStateSnapshot->IsFlipped());
			}			
		}
		else
		{
			DATA_TYPE result;
			if (GetDataFromNode(pNode, &result))
			{
				return result;
			}

			return GetDefaultData();
		}
	}
};
