/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */


#ifndef NDLIB_NAV_ASSERT_H
#define NDLIB_NAV_ASSERT_H

//
// Enable this to make RENDER_ASSERTS into ALWAYS_ASSERTS
//
#ifdef FINAL_BUILD
	#undef ENABLE_NAV_ASSERTS
	#define ENABLE_NAV_ASSERTS	0
#else
	#ifndef ENABLE_NAV_ASSERTS
		#define ENABLE_NAV_ASSERTS	1
	#endif
#endif

#if ENABLE_NAV_ASSERTS
	#define NAV_ASSERT(x)		ALWAYS_ASSERT(x)
	#define NAV_ASSERTF(x, y)	ALWAYS_ASSERTF(x, y)
#else
	#define NAV_ASSERT(x)		ASSERT(x)
	#define NAV_ASSERTF(x, y)	ASSERTF(x, y)
#endif

#endif
