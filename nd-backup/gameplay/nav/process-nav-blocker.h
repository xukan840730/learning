/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nd-locatable.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class DynamicNavBlocker;
class ProcessSpawnInfo;
class StaticNavBlocker;

/// --------------------------------------------------------------------------------------------------------------- ///
class ProcessNavBlocker : public NdLocatableObject
{
public:
	ProcessNavBlocker();
	virtual ~ProcessNavBlocker() override;

	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual U32F GetMaxStateAllocSize() override { return 0; }

	virtual void ProcessUpdate() override;

	void EnableNavBlocker(bool enable);
	void AllocateDynamicNavBlocker();
	void DeallocateDynamicNavBlocker();

private:
	typedef NdLocatableObject ParentClass;

	bool m_enableRequested;

	Point m_registeredPosPs;

	StringId64 m_triggerObject;
	float m_triggerRadius;

	SpawnerNavBlockerType m_type;
	Vector m_boxMinLs;
	Vector m_boxMaxLs;

	StringId64 m_blockProcessType;

	NavPolyHandle m_hNavPoly;
	StaticNavBlocker* m_pStaticNavBlocker;
	DynamicNavBlocker* m_pDynamicNavBlocker;
};

PROCESS_DECLARE(ProcessNavBlocker);
