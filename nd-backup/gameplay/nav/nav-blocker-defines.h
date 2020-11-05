/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/gameplay/nav/nav-assert.h"

namespace Nav
{
	/// --------------------------------------------------------------------------------------------------------------- ///
	// Different blockage types
	enum class StaticBlockageType : U8
	{
		kHuman,
		kInfected,
		kHorse,
		kDog,
		kBuddy,
		kBloater,
		kAccessibility,
		kCount
	};


	// If you change this, please update the static-blockage-mask dc enum in nav-character-defines.dcx to match
	typedef U16 StaticBlockageMask;

	/// --------------------------------------------------------------------------------------------------------------- ///
	static CONST_EXPR StaticBlockageMask ToStaticBlockageMask(StaticBlockageType t) { return 1U << U8(t); }

	/// --------------------------------------------------------------------------------------------------------------- ///
	// If you change this, please update the static-blockage-mask dc enum in nav-character-defines.dcx to match
	static CONST_EXPR StaticBlockageMask kStaticBlockageMaskNone		  = 0U;
	static CONST_EXPR StaticBlockageMask kStaticBlockageMaskHuman		  = ToStaticBlockageMask(StaticBlockageType::kHuman);
	static CONST_EXPR StaticBlockageMask kStaticBlockageMaskInfected	  = ToStaticBlockageMask(StaticBlockageType::kInfected);
	static CONST_EXPR StaticBlockageMask kStaticBlockageMaskHorse		  = ToStaticBlockageMask(StaticBlockageType::kHorse);
	static CONST_EXPR StaticBlockageMask kStaticBlockageMaskDog			  = ToStaticBlockageMask(StaticBlockageType::kDog);
	static CONST_EXPR StaticBlockageMask kStaticBlockageMaskBuddy		  = ToStaticBlockageMask(StaticBlockageType::kBuddy);
	static CONST_EXPR StaticBlockageMask kStaticBlockageMaskBloater		  = ToStaticBlockageMask(StaticBlockageType::kBloater);
	static CONST_EXPR StaticBlockageMask kStaticBlockageMaskAccessibility = ToStaticBlockageMask(StaticBlockageType::kAccessibility);
	// If you change this, please update the static-blockage-mask dc enum in nav-character-defines.dcx to match

	static CONST_EXPR StaticBlockageMask kStaticBlockageMaskLargeCreature = kStaticBlockageMaskHuman
																			| kStaticBlockageMaskBuddy
																			| kStaticBlockageMaskInfected
																			| kStaticBlockageMaskHorse;

	static CONST_EXPR StaticBlockageMask kStaticBlockageMaskAll = kStaticBlockageMaskHuman | kStaticBlockageMaskInfected
																  | kStaticBlockageMaskHorse | kStaticBlockageMaskDog
																  | kStaticBlockageMaskBuddy | kStaticBlockageMaskBloater
																  | kStaticBlockageMaskAccessibility;

	/// --------------------------------------------------------------------------------------------------------------- ///
	static const char* GetStaticBlockageTypeStr(const StaticBlockageType t)
	{
		switch (t)
		{
		case StaticBlockageType::kHuman:
			return "kHuman";
		case StaticBlockageType::kInfected:
			return "kInfected";
		case StaticBlockageType::kHorse:
			return "kHorse";
		case StaticBlockageType::kDog:
			return "kDog";
		case StaticBlockageType::kBuddy:
			return "kBuddy";
		case StaticBlockageType::kBloater:
			return "kBloater";
		case StaticBlockageType::kAccessibility:
			return "kAccessibility";
		}

		return "<unknown>";
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static void GetStaticBlockageMaskStr(const StaticBlockageMask blockageMask, IStringBuilder* pStrOut)
	{
		const char* spacer = "";

		for (U32F iType = 0; iType < (U32F)StaticBlockageType::kCount; ++iType)
		{
			const StaticBlockageType type = StaticBlockageType(iType);
			const StaticBlockageMask typeMask = ToStaticBlockageMask(type);

			if (0 == (blockageMask & typeMask))
				continue;

			pStrOut->append_format("%s%s", spacer, GetStaticBlockageTypeStr(type));
			spacer = " | ";
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	static StaticBlockageType StaticBlockageTypeFromSid(StringId64 typeId)
	{
		switch (typeId.GetValue())
		{
		case SID_VAL("Human"):
			return StaticBlockageType::kHuman;
		case SID_VAL("Infected"):
			return StaticBlockageType::kInfected;
		case SID_VAL("Horse"):
			return StaticBlockageType::kHorse;
		case SID_VAL("Dog"):
			return StaticBlockageType::kDog;
		case SID_VAL("Buddy"):
			return StaticBlockageType::kBuddy;
		case SID_VAL("Bloater"):
			return StaticBlockageType::kBloater;
		case SID_VAL("Accessibility"):
		case SID_VAL("A11y"):
			return StaticBlockageType::kAccessibility;
		}

		NAV_ASSERTF(false, ("Unknown static nav blockage type '%s'", DevKitOnly_StringIdToString(typeId)));

		return StaticBlockageType::kHuman;
	}

	StaticBlockageMask BuildStaticBlockageMask(const EntityDB* pDb);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// should match the NavBlockerType enum in charter!
enum class SpawnerNavBlockerType
{
	Inherit,
	None,
	Static,
	Dynamic
};

/// --------------------------------------------------------------------------------------------------------------- ///
inline const char* GetSpawnerNavBlockerTypeStr(const SpawnerNavBlockerType t)
{
	switch (t)
	{
	case SpawnerNavBlockerType::Inherit:	return "Inherit";
	case SpawnerNavBlockerType::None:		return "None";
	case SpawnerNavBlockerType::Static:		return "Static";
	case SpawnerNavBlockerType::Dynamic:	return "Dynamic";
	}

	return "<UNKNOWN>";
}

/// --------------------------------------------------------------------------------------------------------------- ///
#ifdef HEADLESS_BUILD
static const U32F kMaxDynamicNavBlockerCount = 128;
static const U32F kMaxStaticNavBlockerCount = 1024;
#else
static const U32F kMaxDynamicNavBlockerCount = 64;
static const U32F kMaxStaticNavBlockerCount = 512;
#endif
typedef BitArray<kMaxDynamicNavBlockerCount> NavBlockerBits;
typedef BitArray<kMaxStaticNavBlockerCount> StaticNavBlockerBits;
