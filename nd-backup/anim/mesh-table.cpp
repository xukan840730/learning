/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/mesh-table.h"
#include "ndlib/nd-frame-state.h"

/// --------------------------------------------------------------------------------------------------------------- ///
I64 MeshTable::s_meshTableModifiedFrame = 0;

/// --------------------------------------------------------------------------------------------------------------- ///
void MeshTable::SetMeshTableModified()
{
	s_meshTableModifiedFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumber;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MeshTable::WasMeshTableModifiedLastFrame()
{
	return s_meshTableModifiedFrame == (EngineComponents::GetNdFrameState()->m_gameFrameNumber-1);
}

