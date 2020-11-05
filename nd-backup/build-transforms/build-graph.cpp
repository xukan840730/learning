/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/stdafx.h"

#include <set>
#include <vector>

#include "tools/pipeline3/build/build-transforms/build-scheduler.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/toolversion.h"

#include "tools/libs/toolsutil/redis-helper.h"
#include "tools/libs/toolsutil/redis-log-manager.h"
#include "tools/libs/toolsutil/redis-unique-connection.h"
#include "tools/libs/toolsutil/strfunc.h"
#include "tools/libs/toolsutil/json_writer.h"

using std::vector;

//#pragma optimize("", off)


/// --------------------------------------------------------------------------------------------------------------- ///
static std::string TransformStatusToDOTStyle(BuildTransformStatus status)
{
	std::string ret;
	switch (status)
	{
	case BuildTransformStatus::kWaitingInputs:
		toolsutils::s_sprintf(ret, "style=filled, color = \"#%2x%2x%2x%2x\"", 55, 126, 184, 127);			// blueish
		break;
	case BuildTransformStatus::kFailed:
		toolsutils::s_sprintf(ret, "style=filled, color = \"#%2x%2x%2x%2x\"", 228, 26, 28, 127);			//red
		break;
	case BuildTransformStatus::kOutputsUpdated:
		toolsutils::s_sprintf(ret, "style=filled, color = \"#%2x%2x%2x%2x\"", 77, 175, 74, 127);			//green
		break;
	case BuildTransformStatus::kResumeNeeded:
		toolsutils::s_sprintf(ret, "style=filled, color = \"#%2x%2x%2x%2x\"", 152, 78, 163, 127);			// light red/salmon
		break;
	default:
		toolsutils::s_sprintf(ret, "style=filled, color = \"#%2x%2x%2x%2x\"", 152, 152, 152, 127);
		break;
	}
	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const std::string& TransformStatusToString(BuildTransformStatus status)
{
	static std::string typenames[] = { "kWaitingInputs", "kFailed", "kOutputsUpdated", "kResumeNeeded" };
	static_assert(sizeof(typenames) / sizeof(*typenames) == static_cast<int>(BuildTransformStatus::kNumStatus), "typenames is missing strings for values of BuildTransformStatus");
	return typenames[size_t(status)];
}

static void Internal_WriteNodeEdgeGraph(const vector<const BuildTransform*>& transformsList, const BuildScheduler& scheduler, toolsutils::JsonStringWriter& outWriter)
{
	std::set<string> uniqueInputOutputsList;

	for (const BuildTransform* pTransform : transformsList)
	{
		for (const auto input : pTransform->GetInputs())
		{
			uniqueInputOutputsList.emplace(input.m_file.AsAbsolutePath());
		}

		for (const auto discoveredDep : pTransform->GetDiscoveredDependencies())
		{
			uniqueInputOutputsList.emplace(discoveredDep.m_path.AsAbsolutePath());
		}

		for (const auto output : pTransform->GetOutputs())
		{
			uniqueInputOutputsList.emplace(output.m_path.AsAbsolutePath());
		}
	}

	outWriter.BeginObject();
	outWriter.BeginArray("edges");

	// dump the whole graph
	// Edges
	{
		for (auto pTransform : transformsList)
		{
			// inputs
			for (auto input : pTransform->GetInputs())
			{
				const auto where = uniqueInputOutputsList.find(input.m_file.AsAbsolutePath());
				if (where != uniqueInputOutputsList.end())
				{
					std::string link;
					toolsutils::s_sprintf(link, "x%08x -> x%08x", (uintptr_t)&(*where), (uintptr_t)pTransform);
					outWriter.Value(link);
					outWriter.emitLine("");
				}
			}

			// discovered dependencies
			for (auto discoveredDep : pTransform->GetDiscoveredDependencies())
			{
				const auto where = uniqueInputOutputsList.find(discoveredDep.m_path.AsAbsolutePath());
				if (where != uniqueInputOutputsList.end())
				{
					std::string link;
					toolsutils::s_sprintf(link, "x%08x -> x%08x", (uintptr_t)&(*where), (uintptr_t)pTransform);
					outWriter.Value(link);
					outWriter.emitLine("");
				}
			}

			//outputs
			for (auto output : pTransform->GetOutputs())
			{
				const auto where = uniqueInputOutputsList.find(output.m_path.AsAbsolutePath());
				if (where != uniqueInputOutputsList.end())
				{
					std::string link;
					toolsutils::s_sprintf(link, "x%08x -> x%08x", (uintptr_t)pTransform, (uintptr_t)&(*where));
					outWriter.Value(link);
					outWriter.emitLine("");
				}
			}

		}
	}
	outWriter.EndArray();
	outWriter.BeginArray("nodes");
	// Nodes
	{
		for (auto& io : uniqueInputOutputsList)
		{
			std::string fullFilename = io;
			std::string nodeName;
			toolsutils::s_sprintf(nodeName, "x%08x", (uintptr_t)&io);
			outWriter.BeginObject();
			outWriter.KeyValue("name", nodeName);
			outWriter.KeyValue("type", "file");
			std::replace(fullFilename.begin(), fullFilename.end(), '\\', '/');
			outWriter.KeyValue("path", fullFilename);
			DataHash fileHash;
			if (scheduler.GetContentHashCollection().GetContentHash(BuildPath(fullFilename), &fileHash))
				outWriter.KeyValue("hash", fileHash.AsText());
			else
				outWriter.KeyValue("hash", "0");
			outWriter.EndObject();
		}

		for (auto pTransform : transformsList)
		{
			const auto &info = *scheduler.GetTransformInfo(pTransform);
			std::string nodeName;
			toolsutils::s_sprintf(nodeName, "x%08x", (uintptr_t)pTransform);
			outWriter.BeginObject();
			outWriter.KeyValue("name", nodeName);
			outWriter.KeyValue("type", "transform");
			outWriter.KeyValue("transformType", pTransform->GetTypeName());
			outWriter.KeyValue("status", TransformStatusToString(info.m_status));
			outWriter.KeyValue("evaluateStartTime", double(info.m_evaluateStartTime));
			outWriter.KeyValue("evaluateEndTime", double(info.m_evaluateEndTime));
			outWriter.KeyValue("m_resumeStartTime", double(info.m_resumeStartTime));
			outWriter.KeyValue("m_resumeEndTime", double(info.m_resumeEndTime));
			outWriter.KeyValue("m_startOrder", info.m_startOrder);
			outWriter.KeyValue("m_completionOrder", info.m_completionOrder);
			//writer.Key("m_postDependencies");		// THIS IS TOO BIG FOR REDIS
			//writer.emitLine(AsJsonString(pTransform->GetPostEvaluateDependencies()));
			outWriter.EndObject();
		}
		//}
	}
	outWriter.EndArray();
	outWriter.EndObject();
}

static void Internal_WriteTransformTable(const vector<const BuildTransform*>& transformsList, const BuildScheduler& scheduler, const string& graphName, string& json)
{
	std::string jsonGraphString = "[";
	// Now, generate the JSON for all nodes
	bool firstTransform = true;
	time_t firstEvaluateTime = 0;
	for (const auto pTransform : transformsList)
	{
		const BuildTransformSchedulerInfo* pInfo = scheduler.GetTransformInfo(pTransform);
		if (pInfo)
		{
			const BuildTransformSchedulerInfo& info = *pInfo;
			const auto& outputs = pTransform->GetOutputs();

			if (!firstTransform)
			{
				jsonGraphString += ",";
			}
			else
			{
				firstEvaluateTime = info.m_evaluateStartTime;
			}

			jsonGraphString += "{";
			jsonGraphString += "\"type\":\"transform\"";

			std::string transformName;
			toolsutils::s_sprintf(transformName, "x%08x", (uintptr_t)pTransform);
			jsonGraphString += ",\"name\":\"" + transformName + "\"";

			jsonGraphString += ",\"buildIndex\":" + toolsutils::ToString(info.m_startOrder);
			jsonGraphString += std::string(",\"evaluated\":") + ((info.m_status == BuildTransformStatus::kOutputsUpdated && info.m_evaluateStartTime == 0) ? "false" : "true");
			jsonGraphString += std::string(",\"buildResult\":") + (info.m_status == BuildTransformStatus::kOutputsUpdated ? "true" : "false");
			jsonGraphString += std::string(",\"validationResult\":") + (pTransform->HasValidationError() ? "false" : "true");
			jsonGraphString += ",\"transformType\":\"" + pTransform->GetTypeName() + "\"";

			bool firstOutput = true;
			jsonGraphString += ",\"outputs\":[";
			for (const auto& output : outputs)
			{
				if (!firstOutput)
					jsonGraphString += ",";
				jsonGraphString += "{";
				jsonGraphString += "\"buildPath\": \"" + output.m_path.AsPrefixedPath() + "\"";
				jsonGraphString += ",\"buildBucket\": \"" + GetBucketFromBuildPath(output.m_path) + "\"";
				DataHash contentHash;
				bool success = scheduler.GetContentHashCollection().GetContentHash(output.m_path, &contentHash);
				if (success)
				{
					// get size 
					const int64_t size = DataStore::QueryDataSize(BuildFile(output.m_path, contentHash));

					jsonGraphString += ",\"contentHash\": \"" + contentHash.AsText() + "\"";
					jsonGraphString += ",\"size\":" + toolsutils::ToString(size);
				}
				else
				{
					if (info.m_status == BuildTransformStatus::kWaitingInputs)
						jsonGraphString += ",\"contentHash\": \"Missing Inputs\"";
					else
						jsonGraphString += ",\"contentHash\": \"Failed Build\"";
				}
				jsonGraphString += "}";
				firstOutput = false;
			}
			jsonGraphString += "]";

			const BuildPath depFilePath = GetDependenciesFilePath(pTransform);
			DataHash depFileHash;
			const bool depSuccess = scheduler.GetContentHashCollection().GetContentHash(depFilePath, &depFileHash);
			if (depSuccess)
			{
				jsonGraphString += std::string(",\"depFileHash\":\"") + depFileHash.AsText() + "\"";
			}

			jsonGraphString += ",\"buildReasons\":[";
			firstOutput = true;
			for (const auto& reason : pTransform->GetDepMismatches())
			{
				if (!firstOutput)
					jsonGraphString += ",";

				jsonGraphString += "\"" + reason + "\"";
				firstOutput = false;
			}
			jsonGraphString += "]";

			// Log
			const BuildPath logFilePath = GetLogFilePath(pTransform);
			DataHash logFileHash;
			const bool logSuccess = scheduler.GetContentHashCollection().GetContentHash(logFilePath, &logFileHash);
			if (logSuccess)
			{
				jsonGraphString += std::string(",\"logFileHash\":\"") + logFileHash.AsText() + "\"";
			}

			// Asset dependencies
			const BuildPath assetdFilePath = GetAssetDependenciesFilePath(pTransform);
			DataHash assetdFileHash;
			const bool assetdSuccess = scheduler.GetContentHashCollection().GetContentHash(assetdFilePath, &assetdFileHash);
			if (assetdSuccess)
			{
				jsonGraphString += std::string(",\"assetdFileHash\":\"") + assetdFileHash.AsText() + "\"";
			}

			jsonGraphString += std::string(",\"startTime\":") + toolsutils::ToString(info.m_evaluateStartTime >= firstEvaluateTime ? info.m_evaluateStartTime - firstEvaluateTime : 0);
			if (info.m_resumeEndTime)
			{
				jsonGraphString += std::string(",\"executionTime\":") + toolsutils::ToString((info.m_evaluateEndTime - info.m_evaluateStartTime) + (info.m_resumeEndTime - info.m_resumeStartTime));
				jsonGraphString += std::string(",\"waitTime\":") + toolsutils::ToString(info.m_resumeStartTime - info.m_evaluateEndTime);
			}
			else
			{
				jsonGraphString += std::string(",\"executionTime\":") + toolsutils::ToString(info.m_evaluateEndTime - info.m_evaluateStartTime);
			}

			if (info.m_farmExecutionTime != -1)
				jsonGraphString += std::string(",\"farmExecutionTime\":") + toolsutils::ToString(info.m_farmExecutionTime);
			jsonGraphString += "}";

			firstTransform = false;
		}
	}

	jsonGraphString += "]";

	//We need to use a json library for that!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	const auto& logmanifgo = RedisLogManager::GetInstance().GetInfo();
	string jsonString;
	toolsutils::s_sprintf(jsonString, "{ \"graph\" : %s, \"executionId\" : %lld }", jsonGraphString.c_str(), logmanifgo.m_id);

	json = jsonString;
}


DataHash GetTransformTableKeyHash()
{
	return DataHash::FromText("12345678901234567890123456789012");
}

DataHash GetNodeEdgeGraphKeyHash()
{
	return DataHash::FromText("abcabcabcabcabcabcabcabcabcabcab");
}

BuildPath GetGraphFilePath(uint64_t buildId)
{
	const std::string prefixedPath = std::string(PathPrefix::BUILD_INTERMEDIATE) + "/" + BUILD_GRAPH_FOLDER + "/" + toolsutils::ToString(buildId);
	return BuildPath(prefixedPath);
}

BuildPath GetSchedulerGraphFilePath(long long executionId)
{
	const std::string prefixedPath = std::string(PathPrefix::BUILD_INTERMEDIATE) + "/" + BUILD_GRAPH_FOLDER + +"/schedulergraph_" + toolsutils::ToString(executionId);
	return BuildPath(prefixedPath);
}


//--------------------------------------------------------------------------------------------------------------------//
void WriteSingleAssetWebGraph(const BuildScheduler *const pBuildScheduler, 
							std::string const& graphName, 
							uint64_t buildId, 
							const BuildTransformContext *const pContext)
{
	const BuildPath graphFilePath = GetGraphFilePath(buildId);
	DataHash contentHash;

	std::vector<const BuildTransform*> allContextTransforms = pBuildScheduler->GetContextTransforms(pContext);

	{
		std::string jsonData;
		Internal_WriteTransformTable(allContextTransforms, *pBuildScheduler, graphName, jsonData);

		DataStore::WriteData(graphFilePath, jsonData, &contentHash);

		const DataHash transformTableKeyHash = GetTransformTableKeyHash();
		DataStore::RegisterAssociation(transformTableKeyHash, graphFilePath, contentHash, RegisterAssociationFlags::kAlwaysRegister);
	}

	{
		toolsutils::JsonStringWriter writer;
		Internal_WriteNodeEdgeGraph(allContextTransforms, *pBuildScheduler, writer);
		const std::string jsonData = writer.str();

		DataStore::WriteData(graphFilePath, jsonData, &contentHash);

		const DataHash nodeEdgeGraphKeyHash = GetNodeEdgeGraphKeyHash();
		DataStore::RegisterAssociation(nodeEdgeGraphKeyHash, graphFilePath, contentHash, RegisterAssociationFlags::kAlwaysRegister);
	}
}


//--------------------------------------------------------------------------------------------------------------------//
void WriteSchedulerWebGraph(const BuildScheduler *const pBuildScheduler)
{
	auto& logmanifgo = RedisLogManager::GetInstance().GetInfo();
	if (logmanifgo.m_status != InstanceInfo::UNSET)
	{
		const BuildPath graphFilePath = GetSchedulerGraphFilePath(logmanifgo.m_id);
		DataHash contentHash;

		{
			std::string jsonData;
			Internal_WriteTransformTable(pBuildScheduler->GetAllTransforms(), *pBuildScheduler, "", jsonData);

			DataStore::WriteData(graphFilePath, jsonData, &contentHash);

			const DataHash transformTableKeyHash = GetTransformTableKeyHash();
			DataStore::RegisterAssociation(transformTableKeyHash, graphFilePath, contentHash, RegisterAssociationFlags::kAlwaysRegister);
		}

		{
			toolsutils::JsonStringWriter writer;
			Internal_WriteNodeEdgeGraph(pBuildScheduler->GetAllTransforms(), *pBuildScheduler, writer);
			const std::string jsonData = writer.str();

			DataStore::WriteData(graphFilePath, jsonData, &contentHash);

			const DataHash nodeEdgeGraphKeyHash = GetNodeEdgeGraphKeyHash();
			DataStore::RegisterAssociation(nodeEdgeGraphKeyHash, graphFilePath, contentHash, RegisterAssociationFlags::kAlwaysRegister);
		}
	}
}
