/*
 * Copyright (c) 2018 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ToolParams;

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_EffGenerator : public BuildTransform
{
public:
	BuildTransform_EffGenerator(const ToolParams& params);

	virtual BuildTransformStatus Evaluate();

	std::string AddNewEntry(const std::string& skelName, const std::string& animName);

	bool HasEntries() const { return m_skelAnims.size() > 0; }

private:
	typedef std::map<std::string, std::vector<std::string>> SkelAnimMap;
	typedef SkelAnimMap::value_type SkelAnimMapEntry;

	std::string GetEffNdbFileName(const std::string& skelName, const std::string& animName) const;

	const ToolParams& m_toolParams;

	SkelAnimMap m_skelAnims;
};
