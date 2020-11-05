/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class Demeanor
{
public:
	Demeanor() : m_value(0) {}
	explicit Demeanor(I32 intVal) : m_value(intVal) {}

	void FromI32(const I32 intVal) { m_value = intVal; }
	I32 ToI32() const { return m_value; }

	bool operator!=(const Demeanor& rhs) const { return !(*this == rhs); }
	bool operator==(const Demeanor& rhs) const { return m_value == rhs.m_value; }

	bool operator!=(const I32& rhs) const { return !(*this == rhs); }
	bool operator==(const I32& rhs) const { return m_value == rhs; }

private:
	I32 m_value;
};

/// --------------------------------------------------------------------------------------------------------------- ///
inline bool operator==(const I32& lhs, const Demeanor& rhs)
{
	return lhs == rhs.ToI32();
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline bool operator!=(const I32& lhs, const Demeanor& rhs)
{
	return !(lhs == rhs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
enum GunState
{
	kGunStateHolstered,
	kGunStateOut,
	kGunStateMax
};

/// --------------------------------------------------------------------------------------------------------------- ///
enum ConfigPriority
{
	kConfigPriorityNormal = 0, // for use by AI code
	kConfigPriorityHigh,	   // for use by AI code, overrides normal
	kConfigPriorityScript,	 // for use by designers
	kConfigPriorityCount
};

/// --------------------------------------------------------------------------------------------------------------- ///
enum AimIndex
{
	kAimIndexPrimary,
	kAimIndexAlternate,
	kAimIndexCount,
};

/// --------------------------------------------------------------------------------------------------------------- ///
extern const char* GetConfigPriorityName(const ConfigPriority pri);
