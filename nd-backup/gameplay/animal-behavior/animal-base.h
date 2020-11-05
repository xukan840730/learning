/*
* Copyright (c) 2017 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "gamelib/gameplay/nd-simple-object.h"

class NdAnimalBase : public NdSimpleObject
{
	typedef NdSimpleObject ParentClass;

public:

	FROM_PROCESS_DECLARE(NdAnimalBase);

	virtual void DebugDrawAnimal(ScreenSpaceTextPrinter* pPrinter) const = 0;
};

PROCESS_DECLARE(NdAnimalBase);

FWD_DECL_PROCESS_HANDLE(NdAnimalBase);

//----------------------------------------------------------------------------------//
// PerchLocationResult
//----------------------------------------------------------------------------------//
struct PerchLocationResult
{
	enum Type
	{
		kNone,
		kPerch,
		kGround,
	};

	PerchLocationResult() {}
	PerchLocationResult(Type type, const BoundFrame& loc)
		: m_type(type)
		, m_locator(loc)
	{}

	Type m_type;
	BoundFrame m_locator;
};

