/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/build-transforms/build-transform-dc-built-ins.h"

#include "common/util/stringutil.h"

#include "tools/libs/racketlib/util.h"
#include "tools/libs/toolsutil/command-job.h"

#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-dco.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/path-converter.h"

// #pragma optimize("", off) // uncomment when debugging in release mode

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_DcBuiltins::BuildTransform_DcBuiltins(const DcoConfig& dcoConfig, const BuildTransformContext* pContext)
	: BuildTransform("DcBuiltIns", pContext), m_dcoConfig(dcoConfig)

{
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_DcBuiltins::Evaluate()
{
	RacketPrecompileThreadSafe("built-ins");

	const TransformOutput& codeOutput = GetOutput("code.h");
	const std::string codeDstPath = codeOutput.m_path.AsAbsolutePath();
	
	const std::string codeCmd = CreateGeneratedHeaderBuildCommand(m_dcoConfig, codeDstPath);

	const std::string workingDir = PathConverter::ToAbsolutePath(PathPrefix::SRCCODE);

	if (CommandShell* pCommand = CommandShell::ConstructAndStart(codeCmd, workingDir))
	{
		pCommand->Join();

		const int ret = pCommand->GetReturnValue();

		INOTE_VERBOSE("\nExit Code: %d\nOutput Was:\n%s\n", ret, StringToUnixLf(pCommand->GetOutput()).c_str());

		if (ret != 0)
		{
			IERR("make-module-index failed!\n", ret);
			IERR("\nExit Code: %d\nOutput Was:\n%s\n", ret, StringToUnixLf(pCommand->GetOutput()).c_str());

			return BuildTransformStatus::kFailed;
		}
	}
	else
	{
		IABORT("Failed to create & start command '%s'\n", codeCmd.c_str());
		return BuildTransformStatus::kFailed;
	}

	DataStore::UploadFile(codeDstPath, codeOutput.m_path, nullptr, DataStorage::kAllowAsyncUpload);

	const TransformInput& builtInsInput = GetInput("dc-builtins.h");
	const TransformOutput& builtInsOutput = GetOutput("dc-builtins.h");

	DataStore::UploadFile(builtInsInput.m_file.AsAbsolutePath(),
										   builtInsOutput.m_path,
										   nullptr,
										   DataStorage::kAllowAsyncUpload);

	return BuildTransformStatus::kOutputsUpdated;
}
