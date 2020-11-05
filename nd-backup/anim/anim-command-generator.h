/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class ArtItemSkeleton;
class EffectList;

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmdGenContext
{
	const ArtItemSkeleton* m_pAnimateSkel = nullptr;
	const FgAnimData* m_pAnimData = nullptr;

	U32 m_pass = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmdGenLayerContext : public AnimCmdGenContext
{
	AnimCmdGenLayerContext(const AnimCmdGenContext& base) : AnimCmdGenContext(base) {}

	bool m_instanceZeroIsValid = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class IAnimCmdGenerator
{
public:
	virtual ~IAnimCmdGenerator() {}
	virtual void CreateAnimCmds(const AnimCmdGenLayerContext& context, AnimCmdList* pAnimCmdList, U32F outputInstance) const = 0;
	virtual const Locator EvaluateChannel(StringId64 channelName) const;
	virtual void FillEffectList(EffectList* pEffectList){};
	virtual void DebugPrint(MsgOutput output) const {}
	virtual void Step(F32 deltaTime) {}
	virtual float GetFadeMult() const { return 1.0f; }
	virtual bool GeneratesPose() const { return true; }
	virtual void Freeze(){};
	virtual I32F GetFeatherBlendTableEntry() const { return -1; }
};
