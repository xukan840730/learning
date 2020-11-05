/*
 * Copyright (c) 2020 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "tools/pipeline3/build/build-transforms/build-transform-scheduler-log.h"

#include "tools/pipeline3/common/blobs/data-store.h"

BuildTransform_SchedulerLog::BuildTransform_SchedulerLog(IMsg::CaptureBuffer& captureBuffer) 
	: BuildTransform("SchedulerLog"), m_captureBuffer(captureBuffer)
{
	SetDependencyMode(BuildTransform::DependencyMode::kIgnoreDependency);
}

BuildTransformStatus BuildTransform_SchedulerLog::Evaluate()
{
	INOTE_VERBOSE("%s", m_captureBuffer.GetText().c_str());
	DataStore::WriteData(GetFirstOutputPath(), "");
	return BuildTransformStatus::kOutputsUpdated;
}