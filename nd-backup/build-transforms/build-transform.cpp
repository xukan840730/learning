/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/stdafx.h"
#include <algorithm>
#include <string>
#include <locale>
#include "tools/libs/toolsutil/strfunc.h"
#include "tools/pipeline3/build/tool-params.h"
#include "tools/pipeline3/build/build-transforms/build-transform.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-scheduler.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/path-converter.h"


using std::string;
using std::vector;

//#pragma optimize("", off) // uncomment when debugging in release mode

static std::string MyBuildTimeString(int64_t value)
{
	char timeString[128];
	time_t& timeValue = value;
	tm  localtime;
	if (_localtime64_s(&localtime, &timeValue) == 0)
	{
		size_t length = strftime(timeString, 128, "%Y-%m-%d %H:%M:%S", &localtime);
		snprintf(timeString + length, 128 - length, "/(%lld)", value);
		return std::string(timeString);
	}
	else
		return "BAD-TIME-VALUE";
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform::RegisterDiscoveredDependency(const BuildPath& depFilePath, int depDepth)
{
	DiscoveredDependency newDep;
	newDep.m_path = depFilePath;
	newDep.m_depthLevel = depDepth;

	RegisterDiscoveredDependency(newDep);
}


/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform::RegisterDiscoveredDependency(const DiscoveredDependency& newDependency)
{
	for (auto& dep : m_discoveredDependencies)
	{
		if (dep.m_path == newDependency.m_path)
		{
			// If we roped in this dependency at a higher level than previously encountered, then
			// we need to record that.
			if (dep.m_depthLevel > newDependency.m_depthLevel)
				const_cast<DiscoveredDependency&>(dep).m_depthLevel = newDependency.m_depthLevel;
			return;
		}
	}

	m_discoveredDependencies.insert(newDependency);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform::RegisterOutputContentHash(const BuildPath& path, const DataHash& contentHash)
{
	// This should apply to every output but can't be enforced just yet
	if (contentHash == DataHash())
	{
		IABORT("Attempting to register an empty content hash for output [%s]", path.AsPrefixedPath().c_str());
	}

	bool outputFound = false;
	for (const auto& output : GetOutputs())
	{
		if (output.m_path == path)
		{
			m_outputContentHashes.RegisterContentHash(path, contentHash);
			outputFound = true;
			break;
		}
	}
	
	if (!outputFound)
	{
		IABORT("Attempting to register an output hash for a non-existing output [%s]", path.AsPrefixedPath().c_str());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform::ParseJobOutput(const std::string& buildOutput,
									std::vector<std::string>& warnings,
									std::vector<std::string>& errors,
									ContentHashCollection& contentHashes)
{
	const size_t truncatedOutput_pos = buildOutput.find("[FarmAgent] Output exceeded max output size");
	if (truncatedOutput_pos != std::string::npos)
	{
		IERR("Fatal error! The output of the job exceeded the maximum amount. This breaks the dependency system.");
		errors.push_back("Output exceeded max output size");
		return;
	}
	
	// scan the output of the program for errors and warnings
	// store them is an array in order for later printing
	size_t head_pos = 0;
	size_t tail_pos = 0;
	while (head_pos != std::string::npos)
	{
		head_pos = buildOutput.find("ERROR:", head_pos);
		if (head_pos != std::string::npos)
		{
			tail_pos = buildOutput.find('\n', head_pos);
			head_pos += strlen("ERROR:");
			if (tail_pos != std::string::npos)
				errors.push_back(buildOutput.substr(head_pos, tail_pos - head_pos));
			else
				errors.push_back(buildOutput.substr(head_pos));
			head_pos = tail_pos;
		}
	}

	head_pos = 0;
	tail_pos = 0;
	while (head_pos != std::string::npos)
	{
		head_pos = buildOutput.find("WARN:", head_pos);
		if (head_pos != std::string::npos)
		{
			tail_pos = buildOutput.find('\n', head_pos);
			head_pos += strlen("WARN:");
			if (tail_pos != std::string::npos)
				warnings.push_back(buildOutput.substr(head_pos, tail_pos - head_pos));
			else
				warnings.push_back(buildOutput.substr(head_pos));
			head_pos = tail_pos;
		}
	}

	head_pos = 0;
	tail_pos = 0;
	while (head_pos != std::string::npos)
	{
		head_pos = buildOutput.find("Content Hash:", head_pos);
		if (head_pos != std::string::npos)
		{
			tail_pos = buildOutput.find('\n', head_pos);
			
			const std::string line = tail_pos != std::string::npos ? buildOutput.substr(head_pos, tail_pos - head_pos) : buildOutput.substr(head_pos);

			const size_t firstTick = line.find("'");
			const size_t lastTick = line.find("'", firstTick + 1);
			const size_t firstBracket = line.find("[", lastTick + 1);
			const size_t lastBracket = line.find("]", firstBracket + 1);
			const std::string absContentPath = line.substr(firstTick + 1, lastTick - firstTick - 1);

			BuildPath contentPath(absContentPath);	// buildfiletype doesn't matter here, content hash collection only stores prefix paths

			const std::string contentHashStr = line.substr(firstBracket + 1, lastBracket - firstBracket - 1);
			const DataHash contentHash = DataHash::FromText(contentHashStr);
			
			// hack: it is necessary to call DoesDataExist here, because it adds an entry to the DataStore's internal
			// list of known files, which allows the association for this piece of data to be uploaded asynchronously
			// also: this validation is a good idea, regardless. 
			const bool dataExists = DataStore::DoesDataExist(BuildFile(contentPath, contentHash));
			if (!dataExists)
			{
				IABORT("Transform claimed to write %s#%s but data does not exist!", contentPath.AsPrefixedPath().c_str(), contentHash.AsText().c_str());
			}

			contentHashes.RegisterContentHash(contentPath, contentHash);

			
			head_pos = tail_pos;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform::SetInput(const TransformInput& input)						
{
	if (HasUpdatedOutputs())
	{
		IABORT("Trying to modify inputs to BuildTransform '%s' after outputs have already been updated. This is a programmer error.",
			   GetTypeName().c_str());
	}
	m_inputs.clear();
	m_inputs.push_back(input);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform::SetInputs(const std::vector<TransformInput>& inputs)
{
	if (HasUpdatedOutputs())
	{
		IABORT("Trying to modify inputs to BuildTransform '%s' after outputs have already been updated. This is a programmer error.",
			   GetTypeName().c_str());
	}

	m_inputs.clear();
	for (const auto& input : inputs)
	{
		m_inputs.push_back(input);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform::SetOutput(const TransformOutput& output)
{
	if (HasUpdatedOutputs())
	{
		IABORT("Trying to modify outputs to BuildTransform '%s' after outputs have already been updated. This is a programmer error.",
			   GetTypeName().c_str());
	}

	m_outputs.clear();
	m_outputs.push_back(output);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform::SetOutputs(const std::vector<TransformOutput>& outputs)
{
	if (HasUpdatedOutputs())
	{
		IABORT("Trying to modify outputs to BuildTransform '%s' after outputs have already been updated. This is a programmer error.",
			   GetTypeName().c_str());
	}

	m_outputs.clear();
	m_outputs.reserve(outputs.size());
	
	std::set<std::string> unique;
	
	for (const TransformOutput& output : outputs)
	{
		if (unique.find(output.m_path.AsPrefixedPath()) != unique.end())
		{
			IWARN("Trying to add output %s multiple times for transform %s",
				  output.m_path.AsAbsolutePath().c_str(),
				  GetTypeName().c_str());
		}
		else
		{
			m_outputs.push_back(output);
			unique.insert(output.m_path.AsPrefixedPath());
		}
	
	}

	if (m_outputs.size() != outputs.size())
	{
		IABORT("Aborting because of duplicate output entries");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform::AddInput(const TransformInput &input)
{
	m_inputs.push_back(input);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform::AddOutput(const TransformOutput &output)
{
	m_outputs.push_back(output);
}

std::string BuildTransform::GetOutputConfigString() const
{
	std::stringstream ss;
	for (int i = 0; i < GetOutputs().size(); i++)
	{
		ss << GetOutputs()[i].m_path.AsPrefixedPath();
		if (i != GetOutputs().size() - 1)
			ss << "\n";
	}

	std::vector<std::pair<std::string, std::string>> configPairs;
	m_preEvaluateDependencies.GetConfigPairs(configPairs);

	if (!configPairs.empty())
	{
		if (GetOutputs().size())
			ss << "\n";

		for (int i = 0; i < configPairs.size(); i++)
		{
			ss << configPairs[i].first << "=" << configPairs[i].second;
			if (i != configPairs.size() - 1)
				ss << "\n";
		}
	}
	
	const std::string outputConfigStr = ss.str();
	DataHash hash = ToDataHash(outputConfigStr);

	return GetFirstOutput().m_path.AsPrefixedPath() + "#" + hash.AsText();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform::AddBuildTransform(BuildTransform* newTransform) const
{
	const vector<const BuildTransformContext*>& contextList = m_scheduler->GetTransformContexts(this);
	m_scheduler->AddBuildTransform(newTransform, contextList);

	if (GetEvaluationMode() != BuildTransform::EvaluationMode::kNormal &&
		newTransform->GetEvaluationMode() == BuildTransform::EvaluationMode::kNormal)
	{
		newTransform->SetEvaluationMode(GetEvaluationMode());
	}
}

uint64_t BuildTransform::RegisterThreadPoolWaitItem(WorkItemHandle workHandle)
{
	return m_scheduler->RegisterThreadPoolWaitItem(this, workHandle);
}

uint64_t BuildTransform::RegisterFarmWaitItem(FarmJobId jobId)
{
	return m_scheduler->RegisterFarmWaitItem(this, jobId);
}

Farm& BuildTransform::GetFarmSession() const
{
	return m_scheduler->GetFarmSession();
}

void BuildTransform::SetScheduler(BuildScheduler* scheduler)
{
	m_scheduler = scheduler;
}

void BuildTransform::WriteAssetDependenciesToDataStore(const DataHash& keyHash) const
{
	const std::string assetdJsonStr = m_assetDeps.ToJsonStr();
	const BuildPath assetdFilePath = GetAssetDependenciesFilePath(this);
	DataHash assetdContentHash;
	DataStore::WriteData(assetdFilePath, assetdJsonStr, &assetdContentHash, DataStorage::kAllowAsyncUpload);

	DataStore::RegisterAssociation(keyHash, assetdFilePath, assetdContentHash);
	m_scheduler->RegisterContentHash(assetdFilePath, assetdContentHash);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static std::string ToWinPath(const std::string& srce)
{
	std::string temp(srce);
	std::replace(temp.begin(), temp.end(), '/', '\\');
	return temp;
}


/// --------------------------------------------------------------------------------------------------------------- ///
static std::string ToUPath(const std::string& srce)
{
	std::string temp(srce);
	std::replace(temp.begin(), temp.end(), '\\', '/');
	return temp;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GatherFilesInFolderRecursively(std::set<std::string>& foundFiles,
									const std::string& searchPath,
									const std::vector<std::string>& allowedExtensions,
									FileDateCache& fdc)
{
	std::map<std::string, FileIO::FileTime> fileTimes;
	FileIO::readDirFileTimes(searchPath.c_str(), fileTimes);
	for (auto iter = fileTimes.begin(); iter != fileTimes.end(); iter++)
		fdc.addFileTime(searchPath + FileIO::separator + iter->first, iter->second);

	const std::string winPath = ToWinPath(searchPath);
	const std::string searchPattern = winPath + "\\*";

	std::vector<_finddata_t> files;
	intptr_t hFind;
	_finddata_t FindFileData;

	hFind = _findfirst(searchPattern.c_str(), &FindFileData);

	if (hFind == -1)
	{
		//		printf ("Invalid File Handle. GetLastError reports %d\n", GetLastError ());
		return;
	}
	files.push_back(FindFileData);
	while (_findnext(hFind, &FindFileData) == 0)
	{
		files.push_back(FindFileData);
	}
	DWORD  dwError = GetLastError();
	_findclose(hFind);

	std::vector<_finddata_t>::iterator filesEnd = files.end();
	for (std::vector<_finddata_t>::iterator it = files.begin(); it != filesEnd; ++it)
	{
		if (((*it).attrib & _A_SUBDIR) == 0)
		{
			if ((*it).name[0] != '.')
			{
				const std::string filename((*it).name);
				const size_t extPos = filename.find_last_of(".");
				const std::string basename = filename.substr(0, extPos);
				const std::string extension = filename.substr(extPos + 1);

				if (allowedExtensions.empty())
				{
					if (filename != "Thumbs.db")
						foundFiles.insert(searchPath + "/" + filename);
				}
				else
				{
					bool allowed = false;
					for (auto& ext : allowedExtensions)
					{
						if (extension == ext)
						{
							allowed = true;
							break;
						}
					}
					if (allowed)
						foundFiles.insert(searchPath + "/" + filename);
				}
			}
		}
	}

	// recurse for each directory
	std::string dot(".");
	std::string dotdot("..");
	for (std::vector<_finddata_t>::iterator it = files.begin(); it != filesEnd; ++it)
	{
		if (((*it).attrib & _A_SUBDIR) != 0)
		{
			std::string subdir((*it).name);
			if (subdir.compare(dot) != 0 && subdir.compare(dotdot) != 0)
			{
				const std::string subpath = searchPath + "/" + (*it).name;
				GatherFilesInFolderRecursively(foundFiles, subpath, allowedExtensions, fdc);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GatherFilesInFolderRecursivelyAsMap(std::map<std::string, std::string>& fileMap,
										 const std::string& searchPath,
										 const std::vector<std::string>& allowedExtensions,
										 FileDateCache& fdc)
{
	std::set<std::string> foundFiles;
	GatherFilesInFolderRecursively(foundFiles, searchPath, allowedExtensions, fdc);

	for (const std::string& path : foundFiles)
	{
		size_t lastSlash = path.find_last_of('/');
		if (lastSlash != std::string::npos)
			fileMap[path.substr(lastSlash + 1)] = FileIO::toLowercase(path.c_str());
		else
			fileMap[path] = FileIO::toLowercase(path.c_str());
	}
}
