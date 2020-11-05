/*
 * Copyright (c) 2018 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "tools/pipeline3/build/build-transforms/build-transform-archive-ingame-shaders.h"

#include "common/util/fileio.h"
#include "common/util/stringutil.h"

#include "tools/libs/toolsutil/command-job.h"
#include "tools/libs/toolsutil/filename.h"
#include "tools/libs/toolsutil/sndbs.h"
#include "tools/libs/toolsutil/temp-files.h"

#include "tools/pipeline3/common/blobs/data-store.h"

#include <fstream>

//#pragma optimize("", off)

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_ArchiveIngameShaders::BuildTransform_ArchiveIngameShaders(const IngameShadersConfig &config,
																		 const BuildTransformContext *pContext)
	: BuildTransform("ArchiveIngameShaders", pContext)
	, m_config(config)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_ArchiveIngameShaders::Evaluate()
{
	SnDbs::StopProject(m_config.m_sndbsProject);

	std::string tmpDir;
	CreateTemporaryLocalDir(tmpDir, FileIO::MakeGloballyUnique("ArchiveIngameShaders"));

	std::string unmodifiedTmpDir = tmpDir;

	tmpDir = StringUtil::replaceAll(tmpDir, "/", FileIO::separator);
	tmpDir = StringUtil::replaceAll(tmpDir, "\\", FileIO::separator);
	tmpDir = tmpDir + FileIO::separator;

	std::string shaderPathPrefix = "shaders" + FileIO::separator + "bytecode" + FileIO::separator;
	std::string srcPathPrefix = "shaders" + FileIO::separator + "src" + FileIO::separator;

	FileIO::makeDirPath((tmpDir + shaderPathPrefix).c_str());
	FileIO::makeDirPath((tmpDir + srcPathPrefix).c_str());

	const BuildPath &archivePath = GetOutputPath("Archive");
	std::string archiveTmpPath = tmpDir + archivePath.ExtractFilename();

	// Create the contents of the archive manifest
	std::string manifestTmpPath = tmpDir + "manifest.xml";
	std::ofstream manifest(manifestTmpPath, std::ofstream::out | std::ofstream::trunc);
	manifest << "<psarc>\n";
	manifest << "\t<create archive=\"" << archiveTmpPath << "\" overwrite=\"true\">\n";
	manifest << "\t\t<compression enabled=\"false\" />\n";
	manifest << "\t\t<strip regex=\"" << tmpDir << "\" />\n";

	for (auto &input : GetInputs())
	{
		std::string filename = input.m_file.ExtractFilename();

		std::string path;
		if (input.m_type == TransformInput::kSourceFile)
		{
			path = tmpDir + srcPathPrefix + filename;
			FileIO::copyFile(path.c_str(), input.m_file.AsAbsolutePath().c_str());
		}
		else
		{
			path = tmpDir + shaderPathPrefix + filename;
			DataStore::DownloadFile(path, input.m_file);
		}

		manifest << "\t\t<file path=\"" << path << "\" />\n";
	}

	manifest << "\t</create>\n</psarc>";
	manifest.close();

	// Create the archive
	std::string cmd = m_config.m_psarcArchiver + " create --xml=" + manifestTmpPath;
	if (CommandShell *pCommand = CommandShell::ConstructAndStart(cmd.c_str()))
	{
		pCommand->Join();

		int ret = pCommand->GetReturnValue();
		if (ret != 0)
		{
			IERR("Archiver exited with code %d!\nCommand: %s\nOutput: %s\n", ret,
				 cmd.c_str(), pCommand->GetOutput().c_str());
			return BuildTransformStatus::kFailed;
		}

		DataStore::UploadFile(archiveTmpPath, archivePath);
		FileIO::deleteDirectory(unmodifiedTmpDir.c_str());
	}
	else
	{
		IERR("Failed to create and start command '%s'\n", cmd.c_str());
		FileIO::deleteDirectory(unmodifiedTmpDir.c_str());
		return BuildTransformStatus::kFailed;
	}

	return BuildTransformStatus::kOutputsUpdated;
}
