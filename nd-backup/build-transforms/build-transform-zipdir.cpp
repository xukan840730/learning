#include "tools/libs/toolsutil/strfunc.h"
#include "tools/libs/3rdparty/miniz-2.0.8/miniz.h"
#include "tools/libs/toolsutil/ndio.h"

#include "tools/pipeline3/common/blobs/blob-cache.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/files-for-spec.h"
#include "tools/pipeline3/common/path-converter.h"

#include "build-scheduler.h"
#include "build-transform-zipdir.h"
#include "tools/pipeline3/toolversion.h"

//#pragma optimize("", off)

static bool ZipFiles(mz_zip_archive& zipArchive, const std::string& rootDir, const std::vector<std::string>& files)
{
	Err oneFileError = Err::kOK;
	for (auto sourceFileName : files)
	{
		std::string filename = rootDir + sourceFileName;
		FILE* sourceFile = ndio::fopen_buf(filename.c_str(), "rb");
		if (!sourceFile)
		{
			IERR("Failed to open file %s.", filename.c_str());
			oneFileError = Err::kErrBadData;
			continue;
		}

		_fseeki64(sourceFile, 0, std::ios_base::end);
		int64_t size = _ftelli64(sourceFile);
		_fseeki64(sourceFile, 0, std::ios_base::beg);

		std::unique_ptr<char[]> buffer = std::make_unique<char[]>(size);
		int64_t bytesRead = fread(buffer.get(), sizeof(char), size, sourceFile);

		fclose(sourceFile);

		if (bytesRead < size)
		{
			IERR("File %s is %zd bytes but only %zd read.", filename.c_str(), size, bytesRead);
			oneFileError = Err::kErrBadData;
			continue;
		}

		{
			if (!mz_zip_writer_add_mem(&zipArchive, sourceFileName.c_str(), buffer.get(), size, MZ_DEFAULT_COMPRESSION))
			{
				const mz_zip_error mzErr = mz_zip_get_last_error(&zipArchive);
				IERR("mz_zip_writer_add_mem failure: (%d) '%s'", mzErr, mz_zip_get_error_string(mzErr));
				oneFileError = Err::kErrGeneral;
			}
		}
	}
	return oneFileError == Err::kOK;
}


class ZipWriteArchive
{
	mz_zip_archive zipArchive;
public:
	operator mz_zip_archive&() { return zipArchive; }
	operator const mz_zip_archive &() const { return zipArchive; }
	mz_zip_archive* get()		{	return &zipArchive;	}
	~ZipWriteArchive()			{ mz_zip_writer_end(&zipArchive); }
};

ZipDirTransform::ZipDirTransform(const std::string& root, const std::string& folder)
	: BuildTransform("ZipDir")
	, m_rootFolder(root)
	, m_folderToZip(folder)
{
}

void ZipDirTransform::PopulatePreEvalDependencies()
{
	std::map<std::string, std::set<std::string>> filesPerDirectory;
	GatherFromSearchPattern(m_rootFolder + m_folderToZip, "*", filesPerDirectory, true);
	size_t iFile = 0;
	for (const auto& dirAndFiles : filesPerDirectory)
	{
		const std::string dir = dirAndFiles.first;
		const std::string rootlessDir = dir.substr(m_rootFolder.size());
		for (const auto& fileName : dirAndFiles.second)
		{
			std::string filePath = dir + fileName;
			int64_t timestamp = FileIO::getFileModTime(filePath.c_str());
			if (timestamp != 0)
			{
				BuildPath buildPath(filePath);

				m_preEvaluateDependencies.SetInputFilenameAndTimeStamp(std::string("file-") + toolsutils::ToString(iFile), buildPath.AsPrefixedPath(), timestamp);
				m_files.push_back(rootlessDir + fileName);
				++ iFile;
			}
		}
	}
}


struct ZipJob : public toolsutils::SafeJobBase
{
	ZipJob() : SafeJobBase(),
		pZipMem(nullptr),
		zipMemSize(0)
	{

	}
	const ZipDirTransform * m_transform;
	ZipWriteArchive m_zipArchive;
	void *pZipMem = nullptr;
	size_t zipMemSize = 0;
};


int ZipDirTransform::EvaluateInThread(toolsutils::SafeJobBase* pJob_)
{
	ZipJob* pJob = (ZipJob*)pJob_;
	const ZipDirTransform* self = pJob->m_transform;
	ZipWriteArchive&  zipArchive = pJob->m_zipArchive;
	bool success = true;
	memset(&(pJob->m_zipArchive), 0, sizeof(ZipWriteArchive));

	if (!mz_zip_writer_init_heap(zipArchive.get(), 1024 * 1024, 1024 * 1024))
	{
		const mz_zip_error mzErr = mz_zip_get_last_error(zipArchive.get());
		IABORT("mz_zip_writer_init_heap failure: (%d) '%s'", mzErr, mz_zip_get_error_string(mzErr));
	}


	bool zipped = ZipFiles(zipArchive, self->m_rootFolder, self->m_files);
	if (!zipped)
		IABORT("files were not zipped");

	void *pZipMem = nullptr;
	size_t zipMemSize = 0;
	if (!mz_zip_writer_finalize_heap_archive(zipArchive.get(), &(pJob->pZipMem), &(pJob->zipMemSize)))
	{
		const mz_zip_error mzErr = mz_zip_get_last_error(zipArchive.get());
		IABORT("mz_zip_writer_finalize_heap_archive failure: (%d) '%s'", mzErr, mz_zip_get_error_string(mzErr));
	}

	return success;
}




BuildTransformStatus ZipDirTransform::Evaluate()
{
	auto pJob = new ZipJob();
	pJob->m_transform = this;
	auto handle = toolsutils::QueueWorkItem([](toolsutils::SafeJobBase* job) { EvaluateInThread(job); }, "ZipDirTransform", pJob);
	RegisterThreadPoolWaitItem(handle);
	return BuildTransformStatus::kResumeNeeded;
}


BuildTransformStatus ZipDirTransform::ResumeEvaluation(const SchedulerResumeItem& resumeItem)
{
	ZipJob* pJob = (ZipJob*)resumeItem.m_threadPoolJob;
	if (pJob->m_shouldAbort)
		return BuildTransformStatus::kFailed;

	OutputBlob blob(pJob->pZipMem, pJob->zipMemSize);
	DataStore::WriteData(GetFirstOutputPath(), blob);

	return BuildTransformStatus::kOutputsUpdated;
}



ZipAppDirTransform::ZipAppDirTransform(const std::string& root, const std::string& appName)	
	: ZipDirTransform(root, appName)
{
}

void ZipAppDirTransform::PopulatePreEvalDependencies()
{
	std::map<std::string, std::set<std::string>> filesPerDirectory;
	GatherFromSearchPattern(m_rootFolder + m_folderToZip, "*", filesPerDirectory, true);
	// this list comes from Y:\ndi\bin\py\publishbin.py
	// (no need to package files we would not have published otherwise)
	std::set<std::string> allowedExtensions = {
		 ".application", ".cg", ".cgfx", ".conf", ".config", ".css", ".dll", ".exe", ".fx",
		 ".manifest", ".map", ".mll", ".py", ".pyd", ".pyo", ".scheme", ".txt", ".ui", ".xml", ".xsd", ".zip" };
	const auto kExtentionsEnd = allowedExtensions.end();
	size_t iFile = 0;
	for (const auto& dirAndFiles : filesPerDirectory)
	{
		const std::string dir = dirAndFiles.first;
		const std::string rootlessDir = dir.substr(m_rootFolder.size());
		for (const auto& fileName : dirAndFiles.second)
		{
			std::string filePath = dir + fileName;
			int64_t timestamp = FileIO::getFileModTime(filePath.c_str());
			size_t where = fileName.rfind('.');

			std::string ext = where != std::string::npos ? fileName.substr(where) : "";
			if (timestamp != 0 && ext.length() != 0 && allowedExtensions.find(ext) != kExtentionsEnd)
			{
				BuildPath buildPath(filePath);
				m_preEvaluateDependencies.SetInputFilenameAndTimeStamp(std::string("file-") + toolsutils::ToString(iFile), buildPath.AsPrefixedPath(), timestamp);
				m_files.push_back(rootlessDir + fileName);
				++iFile;
			}
		}
	}
}