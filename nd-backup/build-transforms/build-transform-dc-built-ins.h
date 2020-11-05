/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform.h"

#include "tools/pipeline3/common/dco.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_DcBuiltins : public BuildTransform
{
public:
	BuildTransform_DcBuiltins(const DcoConfig& dcoConfig, const BuildTransformContext* pContext);

	~BuildTransform_DcBuiltins() {}

	virtual BuildTransformStatus Evaluate() override;

private:
	DcoConfig m_dcoConfig;
};
