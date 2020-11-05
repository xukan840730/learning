/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-layer.h"
#include "ndlib/script/script-manager.h"

class AnimCmdList;
class AnimOverlaySnapshot;
class AnimTable;
struct AnimCmdGenLayerContext;
class NdGameObject;
class AnimStateLayer;

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCopyRemapLayer : public AnimLayer
{
public:
	friend class AnimControl;

	AnimCopyRemapLayer(AnimTable* pAnimTable, AnimOverlaySnapshot* pOverlaySnapshot);

	virtual void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual bool IsValid() const override;
	virtual U32F GetNumFadesInProgress() const override;

	void CreateAnimCmds(const AnimCmdGenLayerContext& context, AnimCmdList* pAnimCmdList) const;

	StringId64 RemapAnimation(StringId64 animName, I32 index) const;
	const AnimStateLayer* GetTargetLayer() const;
	const ArtItemAnim* GetRemappedArtItem(const ArtItemAnim* artItem, I32 layerIdnex) const;
	void SetRemapId(StringId64 remapId, F32 blend);

	virtual void BeginStep(F32 deltaTime, EffectList* pTriggeredEffects, const FgAnimData* pAnimData) override;

private:
	virtual void Setup(StringId64 name, ndanim::BlendMode blendMode) override;
	void Setup(StringId64 name,
			   ndanim::BlendMode blendMode,
			   const NdGameObject* pOwner,
			   const NdGameObject* targetObject,
			   StringId64 layerId,
			   StringId64 symbolMap);

	void DebugPrint(MsgOutput output, U32 priority, const FgAnimData* pAnimData) const;

	NdGameObjectHandle m_hOwnerGameObject;
	NdGameObjectHandle m_hTargetGameObject;
	StringId64 m_targetLayerName;

	static const I32 kMaxLayers = 4;
	ScriptPointer<DC::Map> m_symbolMap[kMaxLayers];
	F32 m_layerFades[kMaxLayers];
	F32 m_layerFadeTime[kMaxLayers];
	I32 m_numLayers = 0;
};
