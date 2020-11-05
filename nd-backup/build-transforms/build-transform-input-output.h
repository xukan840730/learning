/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "tools/pipeline3/common/blobs/build-file.h"
#include "tools/pipeline3/common/path-prefix.h"
#include <string>


/// --------------------------------------------------------------------------------------------------------------- ///
class TransformOutput
{
public:
	enum Flags
	{
		kReplicate = 1 << 0,
		kNondeterministic = 1 << 1,
		kIncludeInManifest = 1 << 2,
		kOutputOnFailure = 1 << 3,
	};

	TransformOutput()
	{
		m_flags = 0;
	}

	explicit TransformOutput(const BuildPath& path)
	{
		m_path = path;
		m_flags = 0;
	}

	TransformOutput(const BuildPath& path, const std::string& nickName)
	{
		m_path = path;
		m_nickName = nickName;
		m_flags = 0;
	}

	TransformOutput(const BuildPath& path, const std::string& nickName, uint32_t flags)
	{
		m_path = path;
		m_nickName = nickName;
		m_flags = flags;
	}

	BuildPath m_path;
	std::string m_nickName;
	uint32_t m_flags;
};


/// --------------------------------------------------------------------------------------------------------------- ///
class TransformInput
{
public:

	enum Type
	{
		kSourceFile,
		kHashedResource
	};

	TransformInput()
	{
		m_type = Type::kHashedResource;
	}

	// Convert from an Output
	TransformInput(const TransformOutput& output)
	{
		m_file = BuildFile(output.m_path, DataHash());
		m_nickName = output.m_nickName;
		m_type = Type::kHashedResource;
	}

	// Two flavors, with or without nickname
	explicit TransformInput(const std::string& path)
	{
		m_file = BuildFile(path, DataHash());
		m_type = PathPrefix::IsSourcePath(m_file.AsPrefixedPath()) ? Type::kSourceFile : Type::kHashedResource;
	}
	explicit TransformInput(const BuildPath& path)
	{
		m_file = BuildFile(path, DataHash());
		m_type = PathPrefix::IsSourcePath(m_file.AsPrefixedPath()) ? Type::kSourceFile : Type::kHashedResource;
	}
	TransformInput(const TransformInput &input, const std::string &nickName)
	{
		m_file = input.m_file;
		m_nickName = nickName;
		m_type = PathPrefix::IsSourcePath(m_file.AsPrefixedPath()) ? Type::kSourceFile : Type::kHashedResource;
	}
	TransformInput(const BuildPath& path, const std::string& nickName)
	{
		m_file = BuildFile(path, DataHash());
		m_nickName = nickName;
		m_type = PathPrefix::IsSourcePath(m_file.AsPrefixedPath()) ? Type::kSourceFile : Type::kHashedResource;
	}

	BuildFile m_file;
	std::string m_nickName;
	Type m_type;
};

