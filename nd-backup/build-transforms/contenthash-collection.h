/*
 * Copyright (c) 2016 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include <string>
#include <utility>
#include <map>

#include "icelib/ndb/ndbmemorystream-helpers.h"		// DataHash

class BuildPath;

class ContentHashCollection
{
	std::map<std::string, DataHash> m_contentHashes;

public:
	bool GetContentHash(const BuildPath& path, DataHash* pContentHash) const;
	bool HasContentHash(const BuildPath& path) const;
	bool RegisterContentHash(const BuildPath& path, const DataHash& contentHash);

	// convenience functions for the scheduler internals
	// Should be cleaned up and replaced with Get/Register function above
	std::map<std::string, DataHash>& GetContainer() { return m_contentHashes; }
	const std::map<std::string, DataHash>& GetContainer() const { return m_contentHashes; }
};





