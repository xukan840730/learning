/*
 * Copyright (c) 2020 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform.h"

class BuildTransform_SchedulerLog : public BuildTransform
{
public:
	BuildTransform_SchedulerLog(IMsg::CaptureBuffer& captureBuffer);

	BuildTransformStatus Evaluate() override;

	IMsg::CaptureBuffer& m_captureBuffer;
};
