/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ANIM_SNAPSHOT_LAYER_H
#define ANIM_SNAPSHOT_LAYER_H

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-layer.h"

class AnimCmdList;
class AnimOverlaySnapshot;
class AnimTable;
class DualSnapshotNode;
struct AnimCmdGenLayerContext;

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSnapshotLayer : public AnimLayer
{
public:
	friend class AnimControl;

	AnimSnapshotLayer(AnimTable* pAnimTable, AnimOverlaySnapshot* pOverlaySnapshot);

	virtual void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual bool IsValid() const override;
	virtual U32F GetNumFadesInProgress() const override;
	
	void CreateAnimCmds(const AnimCmdGenLayerContext& context, AnimCmdList* pAnimCmdList) const;

	const DualSnapshotNode* GetSnapshotNode() { return m_pExternalSnapshotNode; }

private:
	virtual void Setup(StringId64 name, ndanim::BlendMode blendMode) override;
	virtual void Setup(StringId64 name, ndanim::BlendMode blendMode, DualSnapshotNode* snapshotNode);

	void DebugPrint(MsgOutput output, U32 priority) const;

	DualSnapshotNode* m_pExternalSnapshotNode;
};

#endif // ANIM_SNAPSHOT_LAYER_H
