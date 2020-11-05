#include "tools/pipeline3/build/build-transforms/build-transform-upload-folder.h"
#include "tools/pipeline3/build/build-transforms/build-transform-upload-file.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-scheduler.h"
#include "tools/pipeline3/common/blobs/data-store.h"

//#pragma optimize("", off) // uncomment when debugging in release mode

//--------------------------------------------------------------------------------------------------------------------//
BuildTransform_UploadFolder::BuildTransform_UploadFolder(
	const BuildTransformContext* pContext,
	const std::string& baseSrcPath,
	const std::string& baseDestPath,
	const std::vector<std::string>& allowedExtensions,
	bool usePostFixedPath)
	: BuildTransform("UploadFolder", pContext),
	m_baseSrcPath(baseSrcPath),
	m_baseDestPath(baseDestPath),
	m_allowedExtensions(allowedExtensions),
	m_usePostFixedPath(usePostFixedPath)
{
	SetDependencyMode(DependencyMode::kIgnoreDependency);

	GatherFilesInFolderRecursively(m_availableFiles, m_baseSrcPath, m_allowedExtensions, m_pContext->m_buildScheduler.GetFileDateCache());

	int index = 0;
	for (auto &file : m_availableFiles)
	{
		BuildPath path(file);

		char key[128];
		snprintf(key, sizeof key, "available-file-%d", index);
		
		FileIO::FileTime fileTime;
		m_pContext->m_buildScheduler.GetFileDateCache().getFileTime(file, fileTime);

		const int64_t timestamp = fileTime.mStats.st_mtime;

		m_preEvaluateDependencies.SetInputFilenameAndTimeStamp(key, path.AsPrefixedPath(), timestamp);

		index++;
	}
}


//--------------------------------------------------------------------------------------------------------------------//
BuildTransform_UploadFolder::~BuildTransform_UploadFolder()
{
}


//--------------------------------------------------------------------------------------------------------------------//
BuildTransformStatus BuildTransform_UploadFolder::Evaluate()
{
	std::string fileListingJson = "{ \"files\" : [ ";
	bool firstEntry = true;
	for (const auto& filePath : m_availableFiles)
	{
		INOTE_VERBOSE("Found file '%s'", filePath.c_str());

		if (filePath.find_first_of(' ') != std::string::npos)
		{
			IERR("File '%s' contains spaces which is unsupported. Please fix.", filePath.c_str());
			continue;
		}

		BuildTransform_UploadFile* pXform = new BuildTransform_UploadFile();
		TransformInput inputFile(filePath.c_str());
		inputFile.m_type = TransformInput::kSourceFile;
		pXform->SetInput(inputFile);

		if (m_usePostFixedPath)
		{
			// We need to convert this to a proper build output
			std::string prefixedPath = PathConverter::ToPrefixedPath(filePath);
			std::string postPrefix = PathConverter::GetPostPrefix(prefixedPath);
			pXform->SetOutput(TransformOutput(PathPrefix::BUILD_OUTPUT + postPrefix, "unnamed", TransformOutput::kIncludeInManifest));
		}
		else
		{
			std::string baseDestPath = m_baseDestPath;
			if (baseDestPath.back() != '/')
				baseDestPath += "/";
			pXform->SetOutput(TransformOutput(baseDestPath + filePath.substr(m_baseSrcPath.size() + 1), "unnamed", TransformOutput::kIncludeInManifest));
		}
		m_pContext->m_buildScheduler.AddBuildTransform(pXform, m_pContext);

		if (!firstEntry)
			fileListingJson += ",\n";
		firstEntry = false;

		fileListingJson += "\"" + filePath.substr(m_baseSrcPath.length() + 1) + "\"";

	}
	fileListingJson += " ] }";

	// Add one more output...
	std::string filesJsonPath = m_baseDestPath + "/files.json";
	AddOutput(TransformOutput(filesJsonPath, "json", TransformOutput::kIncludeInManifest));
	DataStore::WriteData(filesJsonPath, fileListingJson);

	const BuildPath& dummyOutput = GetFirstOutputPath();
	DataStore::WriteData(dummyOutput, "");

	return BuildTransformStatus::kOutputsUpdated;
}
