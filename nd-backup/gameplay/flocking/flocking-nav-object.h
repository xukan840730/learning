/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef FLOCKING_NAV_OBJECT_H
#define FLOCKING_NAV_OBJECT_H

#include "gamelib/gameplay/nd-simple-object.h"

namespace Flocking
{
	class FlockingAgent;
}

namespace Flocking
{
	class FlockingTestNavObject : public NdSimpleObject
	{
		typedef NdSimpleObject ParentClass;

	public:
		STATE_DECLARE_OVERRIDE(Active);

		virtual Err Init(const ProcessSpawnInfo& spawn) override;
		virtual void OnKillProcess() override;

		void PlaceHolder();

	protected:
		FlockingAgent* m_pFlockingAgent;
	};
}

#endif // FLOCKING_NAV_OBJECT_H


