/*
 * Copyright (c) 2007 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/joint-modifiers/joint-modifier.h"

#include "corelib/memory/relocate.h"
#include "ndlib/anim/anim-debug.h"

/// --------------------------------------------------------------------------------------------------------------- ///
IJointModifier::IJointModifier(FgAnimData::PluginParams* pPluginParams) : m_pPluginParams(pPluginParams)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IJointModifier::Init()
{
	Enable();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IJointModifier::PostInit()
{

}

/// --------------------------------------------------------------------------------------------------------------- ///
void IJointModifier::Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pPluginParams, offset_bytes, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IJointModifier::Shutdown()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IJointModifier::PreAnimBlending()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IJointModifier::Enable()
{
	ANIM_ASSERT(m_pPluginParams);
	m_pPluginParams->m_enabled = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IJointModifier::Disable()
{
	ANIM_ASSERT(m_pPluginParams);
	m_pPluginParams->m_enabled = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IJointModifier::IsEnabled() const
{
	ANIM_ASSERT(m_pPluginParams);
	return m_pPluginParams->m_enabled;
}
