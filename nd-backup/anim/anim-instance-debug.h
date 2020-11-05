/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/nd-subsystem-anim-action.h"

class NdSubsystemMgr;

/// --------------------------------------------------------------------------------------------------------------- ///
class NdAnimActionDebugSubsystem : public NdSubsystemAnimAction
{
	friend class NdSubsystemMgr;

	typedef NdSubsystemAnimAction ParentClass;

public:
	virtual Err Init(const SubsystemSpawnInfo& info) override;

	SUBSYSTEM_UPDATE(PostAnimUpdate);

	void InstancePendingChange(StringId64 layerId, StateChangeRequest::ID requestId, StringId64 changeId, int changeType);
	void InstanceAutoTransition(StringId64 layerId);

	virtual bool GetQuickDebugText(IStringBuilder* pText) const override;

private:
	StringId64 m_changeId = INVALID_STRING_ID_64;
	int m_changeType	  = -1;
};
