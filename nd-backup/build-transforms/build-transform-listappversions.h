/*
 * Copyright (c) 2019 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */
#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform.h"

class ListAppVersionsTransform : public BuildTransform
{
public:

	ListAppVersionsTransform();
	virtual BuildTransformStatus Evaluate() override;
};

