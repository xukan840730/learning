/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef NAV_DEFINES_H
#define NAV_DEFINES_H

#include "corelib/containers/list-array.h"

class NavMesh;
#if ENABLE_NAV_LEDGES
class NavLedgeGraph;
#endif // ENABLE_NAV_LEDGES

typedef ListArray<const NavMesh*> NavMeshArray;
#if ENABLE_NAV_LEDGES
typedef ListArray<const NavLedgeGraph*> NavLedgeGraphArray;
#endif // ENABLE_NAV_LEDGES

namespace Nav
{
	extern const StringId64 kMatchAllBindSpawnerNameId;
}

#endif // NAV_DEFINES_H
