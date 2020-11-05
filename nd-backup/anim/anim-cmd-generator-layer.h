/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-layer.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class AnimOverlaySnapshot;
class AnimTable;
class EffectList;
class IAnimCmdGenerator;
class Locator;
struct AnimCmdGenLayerContext;
struct FgAnimData;

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdGeneratorLayer : public AnimLayer
{
public:
	friend class AnimControl;

	AnimCmdGeneratorLayer(AnimTable* pAnimTable, AnimOverlaySnapshot* pOverlaySnapshot);

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual bool IsValid() const override;

	virtual float GetCurrentFade() const override;

	void CreateAnimCmds(const AnimCmdGenLayerContext& context, AnimCmdList* pAnimCmdList) const;

	const Locator EvaluateAP(StringId64 apChannelName) const;

	void Freeze();

private:
	virtual void Setup(StringId64 name, ndanim::BlendMode blendMode) override;
	virtual void Setup(StringId64 name, ndanim::BlendMode blendMode, IAnimCmdGenerator* pAnimCmdGenerator);
	virtual void BeginStep(F32 deltaTime, EffectList* pTriggeredEffects, const FgAnimData* pAnimData) override;
	virtual void FinishStep(F32 deltaTime, EffectList* pTriggeredEffects) override;

	void DebugPrint(MsgOutput output, U32 priority) const;

	IAnimCmdGenerator* m_pAnimCmdGenerator; //<! Relo: Externally supplied command generator.
};
