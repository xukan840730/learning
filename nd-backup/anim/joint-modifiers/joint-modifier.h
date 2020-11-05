/*
 * Copyright (c) 2007 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-data.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class IJointModifier
{
public:
	IJointModifier(FgAnimData::PluginParams* pPluginParams);
	virtual ~IJointModifier() {}

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	virtual void Init();
	virtual void PostInit();
	virtual void Shutdown();
	virtual void PreAnimBlending();
	virtual void PostAnimBlending() = 0;
	virtual void PostJointUpdate() {}
	virtual void OnTeleport() {}
	virtual void EnterNewParentSpace(const Transform& matOldToNew,
									 const Locator& oldParentSpace,
									 const Locator& newParentSpace)
	{
	}

	virtual void Enable();
	virtual void Disable();
	virtual bool IsEnabled() const;

	virtual void DebugDraw() const {}

	virtual U32F CollectRequiredEndEffectors(StringId64* pJointIdsOut, U32F maxJointIds) const = 0;

protected:
	FgAnimData::PluginParams* GetPluginParams() { return m_pPluginParams; }

private:
	FgAnimData::PluginParams* m_pPluginParams;
};
