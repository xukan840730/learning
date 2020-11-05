/*
 * Copyright (c) 2019 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */


#include "tools/libs/toolsutil/json_writer.h"
#include "tools/pipeline3/common/blobs/blob-cache.h"
#include "tools/pipeline3/common/blobs/data-store.h"


#include "build-transform-listappversions.h"

//#pragma optimize("", off)

ListAppVersionsTransform::ListAppVersionsTransform() : BuildTransform("ListAppVersions")
{

}

//--------------------------------------------------------------------------------------------------------------------//
static void ToJson(const std::map<std::string, std::string>& stringMap, toolsutils::JsonWriter& writer)
{
	writer.BeginObject();
	for (auto& kp : stringMap)
		writer.KeyValue(kp.first, kp.second);
	writer.EndObject();
}

BuildTransformStatus ListAppVersionsTransform::Evaluate()
{
	std::map < std::string, std::string > versionPerApp;
	for (auto& input : GetInputs())
	{
		std::string appString = input.m_nickName;
		// ensure we are properly path delimited
		if ((*appString.begin()) != '/')
			appString = '/' + appString;
		if ((*appString.rbegin()) != '/')
			appString = appString + '/';
		versionPerApp[appString] = input.m_file.GetContentHash().AsText();
	}

	toolsutils::JsonStringWriter writer;
	ToJson(versionPerApp, writer);
	std::string jsonString = writer.Stream().str();

	OutputBlob blob(jsonString.data(), jsonString.size());
	DataStore::WriteData(GetFirstOutputPath(), blob);
	return BuildTransformStatus::kOutputsUpdated;
}
