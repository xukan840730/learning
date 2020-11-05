/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-animation.h"

#include <memory>
#include <unordered_map>

/// --------------------------------------------------------------------------------------------------------------- ///
struct MotionMatchingAnimData
{
	CommonAnimData m_animData;
	AnimExportData m_animExportData;
	TransformOutput m_animNdbFile;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_MotionMatchingExport : public BuildTransform
{
public:
	BuildTransform_MotionMatchingExport(const libdb2::MotionMatchSetPtr& pSet,
										const MotionMatchingAnimData& animData);

	BuildTransformStatus Evaluate() override;

private:
	void PopulatePreEvalDependencies();

	libdb2::MotionMatchSetPtr m_pSet;
	MotionMatchingAnimData m_animData;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_MotionMatching : public BuildTransform
{
public:
	using AnimToDataMap = std::unordered_map<std::string, MotionMatchingAnimData>;

	static std::vector<std::string> CreateTransforms(const BuildTransformContext* const pContext,
													 const libdb2::Actor* const pDbActor,
													 const AnimToDataMap& animToNdbMap);

	static bool ValidateMotionMatchSet(const libdb2::MotionMatchSet& set, const AnimToDataMap& animToNdbMap);

	BuildTransform_MotionMatching(const BuildTransformContext *const pContext,
								const libdb2::MotionMatchSetPtr& pSet);

	BuildTransformStatus Evaluate() override;

private:
	void WriteMotionMatchSet(const libdb2::MotionMatchSet& set, BigStreamWriter& writer) const;
	std::string GetOutputFileName() const;

	libdb2::MotionMatchSetPtr m_pSet;
};
