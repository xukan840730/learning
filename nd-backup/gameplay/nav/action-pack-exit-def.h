/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/process/bound-frame.h"

/// --------------------------------------------------------------------------------------------------------------- ///
struct ActionPackExitDef
{
	const DC::ApExitAnimDef* m_pDcDef = nullptr;

	BoundFrame m_apReference = BoundFrame(kIdentity);
	BoundFrame m_sbApReference = BoundFrame(kIdentity);

	StringId64 m_apRefId = INVALID_STRING_ID_64;

	bool m_mirror = false;
};
