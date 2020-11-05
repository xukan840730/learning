#include "tools/pipeline3/build/build-transforms/build-transform-widgets.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-upload-file.h"
#include "tools/pipeline3/build/build-transforms/build-transform-upload-folder.h"
#include "tools/pipeline3/build/build-transforms/build-transform-pak.h"
#include "tools/pipeline3/build/util/build-spec.h"
#include "tools/pipeline3/build/util/json-dom-utils.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/toolversion.h"

#include "tools/libs/bigstreamwriter/bigstreamwriter.h"
#include "tools/libs/bigstreamwriter/ndi-bo-reader.h"
#include "tools/libs/bigstreamwriter/ndi-bo-writer.h"
#include "tools/libs/libdb2/db2-actor.h"

#include "3rdparty/rapidjson/include/rapidjson/reader.h"
#include "3rdparty/rapidjson/include/rapidjson/document.h"
#include "tools/libs/toolsutil/json_helpers.h"
//#include "tools/libs/toolsutil/json_writer.h"

//#pragma optimize("", off) // uncomment when debugging in release mode

namespace jdom
{
	void WriteRapidObject(Static::Builder& builder, const rapidjson::Value& obj);
	void WriteRapidArray(Static::Builder& builder, const rapidjson::Value& arr);

	void WriteRapidValue(Static::Builder& builder, const rapidjson::Value& val)
	{
		switch (val.GetType())
		{
		case rapidjson::kNullType:
			builder.WriteNull();
			break;
		case rapidjson::kFalseType:
			builder.WriteBool(false);
			break;
		case rapidjson::kTrueType:
			builder.WriteBool(true);
			break;
		case rapidjson::kNumberType:
			if (val.IsInt())
				builder.WriteInt((I64)val.GetInt());
			else if (val.IsUint())
				builder.WriteInt((I64)val.GetUint());
			else if (val.IsInt64())
				builder.WriteInt(val.GetInt64());
			else if (val.IsUint64())
				builder.WriteInt((I64)val.GetUint64());
			else if (val.IsDouble())
				builder.WriteFloat((float)val.GetDouble());
			break;
		case rapidjson::kStringType:
			builder.WriteString(val.GetString());
			break;
		case rapidjson::kObjectType:
			WriteRapidObject(builder, val);
			break;
		case rapidjson::kArrayType:
			WriteRapidArray(builder, val);
			break;
		}
	}

	void WriteRapidObject(Static::Builder& builder, const rapidjson::Value& obj)
	{
		const int capacity = obj.MemberCount();
		builder.BeginObject(capacity);

		auto memberEnd = obj.MemberEnd();
		for (auto memberIt = obj.MemberBegin(); memberIt != memberEnd; ++memberIt)
		{
			const rapidjson::Value& key = memberIt->name;
			const rapidjson::Value& val = memberIt->value;

			const char* keyStr = key.GetString();
			const StringId64 keyId = StringToStringId64(keyStr);
			builder.WriteKey(keyId);

			jdom::WriteRapidValue(builder, val);
		}

		builder.EndObject();
	}

	void WriteRapidArray(Static::Builder& builder, const rapidjson::Value& arr)
	{
		const int capacity = arr.Size();
		builder.BeginArray(capacity, EVariety::kHeterogeneous);

		auto end = arr.End();
		for (auto it = arr.Begin(); it != end; ++it)
		{
			const rapidjson::Value& val = *it;

			jdom::WriteRapidValue(builder, val);
		}

		builder.EndArray();
	}
}

//--------------------------------------------------------------------------------------------------------------------//
BuildTransform_JsonToDomBo::BuildTransform_JsonToDomBo(const BuildTransformContext* pContext)
	: BuildTransform("Widget", pContext)
{
}


//--------------------------------------------------------------------------------------------------------------------//
BuildTransform_JsonToDomBo::~BuildTransform_JsonToDomBo()
{
}


//--------------------------------------------------------------------------------------------------------------------//
BuildTransformStatus BuildTransform_JsonToDomBo::Evaluate()
{
	const BuildFile& widgetFile = GetInputFile("WidgetFile");
	const BuildPath& boPath = GetOutputPath("BoFile");

	// Write an NDB stream
	BigStreamWriter stream(m_pContext->m_toolParams.m_streamConfig);

	// Open the widget file
	rapidjson::Document doc;
	size_t fileSize = 0;
	if (toolsutils::json_helpers::LoadJsonFile(doc, widgetFile.AsAbsolutePath().c_str(), true, &fileSize))
	{
		const size_t heapSize = fileSize * 2; // heuristic memory size based on size of JSON text
		U8* pHeapMemory = new U8[heapSize]; // heuristic memory size based on size of JSON text
		if (pHeapMemory)
		{
			BigStreamWriter::Item* pItem = stream.StartItem(BigWriter::JSON_DOC, widgetFile.AsRelativePath());
			stream.AddLoginItem(pItem, BigWriter::JSON_DOC);

			HeapArena arena;
			arena.Init(pHeapMemory, heapSize);

			jdom::Static::Builder builder(&arena, nullptr);

			jdom::WriteRapidValue(builder, doc);

			builder.Finalize();
			const jdom::Static::Dom* pDom = builder.GetDom();
			U8* const pDomBase = pDom->GetBase();
			size_t const domSize = pDom->GetSize();

			pDom->ConvertPointersToOffsets();

			stream.Write(pDomBase, domSize);

			stream.EndItem();

			delete[] pHeapMemory;
		}
	}

	NdiBoWriter boWriter(stream);
	boWriter.Write();
	DataStore::WriteData(boPath, boWriter.GetMemoryStream());

	return BuildTransformStatus::kOutputsUpdated;
}


//--------------------------------------------------------------------------------------------------------------------//
void BuildModuleWidgets_Configure(	const BuildTransformContext *const pContext, 
									const libdb2::Actor& actor, 
									std::vector<std::string>& widgetBos,
									BuildSpec& buildSpec )
{
	const std::string rootPath = "c:/" + std::string(NdGetEnv("GAMENAME")) + "/data/gui2/";
	const std::string basePath = rootPath + "widgets/";

	if (!buildSpec.IsExtension("widget"))
		IABORT("Build file %s doesn't include a line of the form !extension .widget\n", buildSpec.GetInitFileFullPath());
	buildSpec.Gather(rootPath);

	const std::set<std::string>& files = buildSpec.GetFullPaths();
	const bool verbose = buildSpec.IsVerbose();

	if (verbose)
	{
		std::string logPath = rootPath + "build-log-widgets.txt";
		FILE* pfLog = fopen(logPath.c_str(), "a");

		for (auto relPath : buildSpec.GetRelativePaths())
		{
			char logLine[256];
			snprintf(logLine, sizeof(logLine), "%s\n", relPath.c_str());
			INOTE(logLine);
			if (pfLog)
				fwrite(logLine, 1, strlen(logLine), pfLog);
		}

		if (pfLog)
			fclose(pfLog);
	}

	// For all widgets...
	for (const auto& widgetPath : files)
	{
		const auto relativePathStart = widgetPath.find(basePath);
		if (relativePathStart != std::string::npos)
		{
			std::string relPath = widgetPath.substr(basePath.length());
			//if (relPath.substr(0, 5) == "test/") // exclude test widgets (no, for now just build everything)
			//	continue;

			BuildTransform_JsonToDomBo* pXform = new BuildTransform_JsonToDomBo(pContext);

			pXform->SetInput(TransformInput(widgetPath, "WidgetFile"));
			std::string outputBoPath = pContext->m_toolParams.m_buildPathWidgetBo + relPath + ".bo";
			TransformOutput output(outputBoPath, "BoFile");
			pXform->SetOutput(output);
			pContext->m_buildScheduler.AddBuildTransform(pXform, pContext);

			// Add the .bo file
			widgetBos.push_back(outputBoPath);
		}
	}
}
