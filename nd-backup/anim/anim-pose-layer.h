/*
* Copyright (c) 2004 Naughty Dog, Inc. 
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#ifndef ANIM_POSE_LAYER_H
#define ANIM_POSE_LAYER_H

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-layer.h"

class AnimCmdList;
class AnimOverlaySnapshot;
class AnimTable;
class DualSnapshotNode;
struct AnimCmdGenLayerContext;

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimPoseLayer : public AnimLayer
{
public:
	friend class AnimControl;

	AnimPoseLayer(AnimTable* pAnimTable, AnimOverlaySnapshot* pOverlaySnapshot);

	virtual void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual bool IsValid() const override;
	virtual U32F GetNumFadesInProgress() const override;
	
	void CreateAnimCmds(const AnimCmdGenLayerContext& context, AnimCmdList* pAnimCmdList) const;

	void EnableDeferredExecution(bool f) { m_deferredCapable = f; }
	void SetSnapshotNode(DualSnapshotNode* pNode)
	{
		m_pExternalPoseNode = nullptr;
		m_pExternalSnapshotNode = pNode;
	}

	void SetPoseNode(ndanim::PoseNode* pNode)
	{
		m_pExternalPoseNode = pNode;
		m_pExternalSnapshotNode = nullptr;
	}

private:
	virtual void Setup(StringId64 name, ndanim::BlendMode blendMode) override;
	virtual void Setup(StringId64 name, ndanim::BlendMode blendMode, const ndanim::PoseNode* pPoseNode);
	virtual void Setup(StringId64 name, ndanim::BlendMode blendMode, const DualSnapshotNode* pSnapshotNode);

	void DebugPrint(MsgOutput output, U32 priority) const;

	const ndanim::PoseNode* m_pExternalPoseNode;
	const DualSnapshotNode* m_pExternalSnapshotNode;
	bool m_deferredCapable;
};

#endif // ANIM_POSE_LAYER_H
