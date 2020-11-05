/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "tools/pipeline3/build/build-transforms/contenthash-collection.h"

#include "tools/pipeline3/common/blobs/build-file.h"
#include "tools/libs/toolsutil/color-display.h"
#include "tools/pipeline3/build/tool-params.h"

/// --------------------------------------------------------------------------------------------------------------- ///
bool ContentHashCollection::HasContentHash(const BuildPath& path) const
{
	if (path.AsPrefixedPath().size() == 0 || path.AsPrefixedPath()[0] != '[')
	{
		IABORT("You must use prefixed paths when querying for a content hash");
	}

	return m_contentHashes.find(path.AsPrefixedPath()) != m_contentHashes.end();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ContentHashCollection::GetContentHash(const BuildPath& path, DataHash* pContentHash) const
{
	if (path.AsPrefixedPath().size() == 0 || path.AsPrefixedPath()[0] != '[')
	{
		IABORT("You must use prefixed paths when querying for a content hash");
	}

	const auto findIter = m_contentHashes.find(path.AsPrefixedPath());
	if (findIter != m_contentHashes.end())
	{
		*pContentHash = findIter->second;
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ContentHashCollection::RegisterContentHash(const BuildPath& path, const DataHash& contentHash)
{
	if (path.AsPrefixedPath().empty())
	{
		IABORT("You can't register a hash for a zero-length content path");
	}
	if (path.AsPrefixedPath().size() == 0 || path.AsPrefixedPath()[0] != '[')
	{
		IABORT("You must use prefixed paths when registering a content hash");
	}

	DataHash existingContentHash;
	bool alreadyExist = GetContentHash(path, &existingContentHash);
	if (alreadyExist)
	{
		if (contentHash != existingContentHash)
		{
			IERR("'%s' was created twice with different content hashes : ", path.AsPrefixedPath().c_str());
			IERR("   Previous hash : %s", existingContentHash.AsText().c_str());
			IERR("   New hash :      %s", contentHash.AsText().c_str());
			IABORT("Abort!");
		}
	}
	else
	{
		m_contentHashes[path.AsPrefixedPath()] = contentHash;
		return true;
	}

	return false;
}
