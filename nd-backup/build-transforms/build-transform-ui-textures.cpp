#include "build-transform-ui-textures.h"

#include "build-transform-texture-read-spec.h"
#include "build-transform-textures.h"

#include <texture_tool.h>
#include "tools/libs/bigstreamwriter/ndi-bo-writer.h"
#include "tools/libs/libdb2/db2-actor.h"
#include "tools/libs/toolsutil/filename.h"
#include "tools/libs/toolsutil/json_helpers.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-scheduler.h"
#include "tools/pipeline3/build/util/build-spec.h"
#include "tools/pipeline3/common/4_textures.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/textures-common/texture-builder.h"
#include "tools/pipeline3/textures-common/texture-pakwriter.h"
#include "tools/pipeline3/toolversion.h"

#include "ice/src/tools/icelib/common/filepath.h"

#include "tools/pipeline3/build/build-transforms/build-transform-spawner.h"

#include <vector>
#include <set>
#include <tuple>
#include <cstdlib>
#include <cctype>

#include <windows.h>
#include <Shlwapi.h>

//#pragma optimize("", off)

BuildTransformUITextures::TextureMetaData::TextureMetaData()
	: m_path()
	, m_originalWidth()
	, m_originalHeight()
	, m_uv()
{
	for (int i = 0; i < (int)Resolution::kCount; ++i)
		m_id[i] = LoadedTextureId();
}

BuildTransformUITextures::BuildTransformUITextures(const ToolParams* tool, BuildSpec& buildSpec, libdb2::Gui2Resolutions::EResolution resolutions)
	: BuildTransform("UI Textures")
	, m_toolParams(tool)
	, m_buildSpecFilePath(buildSpec.GetInitFileFullPath())
	, m_resolutions(resolutions)
	, m_compress(buildSpec.IsCompressionEnabled())
	, m_verbose(false)
{
	//SetDependencyMode(DependencyMode::kIgnoreDependency); // no, we now generate correct deps (on the list of .png files to process)

	PopulatePreEvalDependencies(buildSpec);
}

BuildTransformUITextures::~BuildTransformUITextures()
{}

std::string BuildTransformUITextures::GetRootGui2Path() const
{
	return "c:/" + std::string(NdGetEnv("GAMENAME")) + "/data/gui2/";
}

std::string BuildTransformUITextures::GetRootTexturesPath() const
{
	return GetRootGui2Path() + "textures/";
}

std::string BuildTransformUITextures::GetRootWidgetsPath() const
{
	return GetRootGui2Path() + "widgets/";
}

inline std::string::size_type FindFileExtension(const std::string& filePath, const char* desiredExt = nullptr)
{
	std::string::size_type pos = filePath.rfind(".");
	if (desiredExt != nullptr && pos != std::string::npos && filePath.substr(pos) != desiredExt)
		pos = std::string::npos;
	return pos;
}
inline bool HasFileExtension(const std::string& filePath, const char* ext)
{
	return (FindFileExtension(filePath, ext) != std::string::npos);
}

std::string BuildTransformUITextures::AddFilePathSuffix(const std::string& filePath, const std::string& suffix)
{
	std::string::size_type extension = FindFileExtension(filePath, ".png");
	if (extension != std::string::npos)
	{
		std::string result = filePath.substr(0, extension) + suffix + ".png";
		return result;
	}
	return filePath + suffix + ".png"; // should never be used, but just in case a file is lacking the .png extension
}

// trim from start (in place)
static inline void ltrim(std::string& s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
}

// trim from end (in place)
static inline void rtrim(std::string& s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string& s)
{
    ltrim(s);
    rtrim(s);
}

std::string BuildTransformUITextures::GetTextureInfoFullPath(const std::string& textureFullPath) const
{
	const std::string rootPath = GetRootTexturesPath();

	std::string jsonFullPath = textureFullPath;
	std::string::size_type pos = FindFileExtension(jsonFullPath, ".png");
	if (pos != std::string::npos)
		jsonFullPath = jsonFullPath.substr(0, pos);
	jsonFullPath += ".json";

	return jsonFullPath;
}

void BuildTransformUITextures::LoadTextureInfo(const std::string& textureFullPath, TextureInfo& info) const
{
	const std::string rootPath = GetRootTexturesPath();

	std::string jsonFullPath = textureFullPath;
	std::string::size_type pos = FindFileExtension(jsonFullPath, ".png");
	if (pos != std::string::npos)
		jsonFullPath = jsonFullPath.substr(0, pos);
	jsonFullPath += ".json";

	info.m_path = textureFullPath;
	info.m_format = TextureLoader::kOutputRgba8888;
	info.m_mode = Mode::kUnspecified;
	info.m_compress = false;

	FILE* f = fopen(jsonFullPath.c_str(), "rb");
	if (f)
	{
		fseek(f, 0, SEEK_END);
		auto length = ftell(f);
		fseek(f, 0, SEEK_SET);

		char* json = new char[length + 3];
		if (json)
		{
			fread(json, 1, length, f);
			fclose(f);

			json[length++] = '\n';
			json[length++] = '\0';

			rapidjson::Document document;
			if (toolsutils::json_helpers::LoadJsonDocument(document, textureFullPath, json))
			{
				const rapidjson::Value& jsonFormat = toolsutils::json_helpers::TryGetValue(document, "format");
				if (!jsonFormat.IsNull() && jsonFormat.IsString())
				{
					const char* f = jsonFormat.GetString();
					if (f[0] == 'L' && f[1] == '8')
					{
						if (f[2] == 'A' && f[3] == '8' && f[4] == '\0')
						{
							info.m_format = TextureLoader::kOutputL8A8;
						}
						else if (f[2] == '\0')
						{
							info.m_format = TextureLoader::kOutputL8;
						}
					}
					else if (f[0] == 'A' && f[1] == '8' && f[2] == '\0')
					{
						info.m_format = TextureLoader::kOutputA8;
					}
				}

				const rapidjson::Value& jsonCompress = toolsutils::json_helpers::TryGetValue(document, "compress");
				if (!jsonCompress.IsNull())
				{
					if (jsonCompress.IsInt())
						info.m_compress = (jsonCompress.GetInt() != 0);
					else if (jsonCompress.IsBool())
						info.m_compress = jsonCompress.GetBool();
				}

				const rapidjson::Value& jsonMode = toolsutils::json_helpers::TryGetValue(document, "mode");
				if (!jsonMode.IsNull() && jsonMode.IsString())
				{
					const char* m = jsonMode.GetString();
					if (strcmp(m, "Clamp") == 0)
					{
						info.m_mode = Mode::kClamp;
					}
					else if (strcmp(m, "Tile") == 0)
					{
						info.m_mode = Mode::kTile;
					}
				}
			}

			// DONE!
			delete[] json;
		}
	}
}

BuildTransformUITextures::TextureMetaData& BuildTransformUITextures::CreateOneTextureGenerationData(
	const std::string& textureFilename,
	TextureGenerationData& textureSpecList,
	std::vector<TextureMetaData>& listOfMetaData,
	const TextureInfo& info,
	LoadedTextureId id1440p,
	LoadedTextureId id1080p,
	const bool omitTextureSpec) const
{
	const LoadedTextureId kTexIdNone = LoadedTextureId();
	const std::string rootPath = GetRootTexturesPath();

	TextureLoader::TextureGenerationSpec dummySpec;
	TextureLoader::TextureGenerationSpec* pSpec;
	const bool noTextureSpec = (omitTextureSpec); // && !g_disableOmissionOf2160pTextures);
	if (noTextureSpec)
	{
		pSpec = &dummySpec;
	}
	else
	{
		textureSpecList.m_textureSpec.push_back(TextureLoader::TextureGenerationSpec());
		pSpec = &textureSpecList.m_textureSpec.back();
	}

	const bool compress = (m_compress && info.m_compress);

	//generate texture spec
	pSpec->m_numMips = 1;
	switch (info.m_format)
	{
	case TextureLoader::kOutputRgba8888:
		pSpec->m_outputMode = (compress) ? TextureLoader::kOutputCompressedRgbHighQ : TextureLoader::kOutputRgba8888; // BC7 or uncompressed
		break;
	case TextureLoader::kOutputL8A8:
		pSpec->m_outputMode = TextureLoader::kOutputL8A8; // no way to compress this one, at present
		break;
	case TextureLoader::kOutputL8:
		pSpec->m_outputMode = (compress) ? TextureLoader::kOutputCompressedGray : TextureLoader::kOutputL8;
		break;
	case TextureLoader::kOutputA8:
		pSpec->m_outputMode = TextureLoader::kOutputA8; // no way to compress this one, at present
		break;
	}
	pSpec->m_noStreaming = true;
	pSpec->m_maxResolution = 4096;		//4k is 3840 * 2160 so there is no need to have a texture bigger than 4096.
	pSpec->m_resizeToPower2 = compress;
	pSpec->m_addBorderTile = compress;
	pSpec->m_flipMipTexture = false;
	pSpec->m_isGui2Texture = true;

	pSpec->m_inputTextures.push_back(TextureLoader::InputTextureSpec());
	TextureLoader::InputTextureSpec& inputTexture = pSpec->m_inputTextures.back();

	inputTexture.m_bGamma = false;
	inputTexture.m_dataType = TextureLoader::kTextureDataRgba;
	inputTexture.m_filename = textureFilename;
	inputTexture.m_textureType = kTexture2D;

	pipeline3::GenCachedFileName(*pSpec);

	//generate texture meta data
	std::string ndbCachedFilename = pSpec->m_cachedFilename + ".ndb";
	LoadedTextureId textureId = pipeline3::texture_pak_writer::GetTextureNameHash(ndbCachedFilename);
	if (noTextureSpec)
	{
		textureId = LoadedTextureId(~0ULL); // an invalid id, but non-zero
	}

	// metadata for Gui2 system

	size_t relativePathStart = textureFilename.find(rootPath);

	std::string pathToAdd = textureFilename;
	if (relativePathStart != std::string::npos)
		pathToAdd = textureFilename.substr(relativePathStart + rootPath.size());
		
	//if (m_tool && m_tool->m_verbosestats) // no, gets lost amongst all the other verbose output!
	//	INOTE("  %s\n", pathToAdd.c_str());

	const sce::TextureTool::GnmTextureGen* gnmTexture = sce::TextureTool::loadImage(textureFilename.c_str());
	const sce::TextureTool::Image* image = gnmTexture->getImage(0);

	listOfMetaData.push_back(TextureMetaData());
	TextureMetaData& metadata = listOfMetaData.back();

	metadata.m_path = pathToAdd;
	metadata.m_id[(int)Resolution::kCanonical] = textureId;
	metadata.m_id[(int)Resolution::k1440p] = id1440p;
	metadata.m_id[(int)Resolution::k1080p] = id1080p;
	metadata.m_originalWidth = image->getWidth();
	metadata.m_originalHeight = image->getHeight();
	if (compress)
	{
		// add 2-texel border and expand to next power of two in size
		uint32_t borderedWidth  = image->getWidth()  + 2;
		uint32_t borderedHeight = image->getHeight() + 2;
		uint32_t finalWidth     = texture_builder::next_power_2(borderedWidth);
		uint32_t finalHeight    = texture_builder::next_power_2(borderedHeight);

		// (u,v)min = 1-texel border on upper-left
		metadata.m_uv[0] = 1.f / finalWidth;
		metadata.m_uv[1] = 1.f / finalHeight;
		// (u,v)max = ratio of original size over expanded size, plus the 1-texel border on upper-left
		metadata.m_uv[2] = static_cast<float>(metadata.m_originalWidth) / finalWidth   + metadata.m_uv[0];
		metadata.m_uv[3] = static_cast<float>(metadata.m_originalHeight) / finalHeight + metadata.m_uv[1];
	}
	else
	{
		// (u,v)min and (u,v)max with no border pixels and 1:1 scale
		metadata.m_uv[0] = 0.0f;
		metadata.m_uv[1] = 0.0f;
		metadata.m_uv[2] = 1.0f;
		metadata.m_uv[3] = 1.0f;
	}

	return metadata;
}

bool BuildTransformUITextures::CreateTextureGenerationData(const std::vector<TexturePair>& pngList, TextureGenerationData& textureSpecList, std::vector<TextureMetaData>& listOfMetaData) const
{
	textureSpecList.m_textureSpec.reserve(pngList.size());
	listOfMetaData.reserve(pngList.size());
	
	for (const TexturePair& p : pngList)
	{
		const std::string& textureFilename = p.first;
		const unsigned availResolutions = p.second;

		const LoadedTextureId kTexIdNone = LoadedTextureId();
		LoadedTextureId id1440p = kTexIdNone;
		LoadedTextureId id1080p = kTexIdNone;

		TextureInfo info;
		LoadTextureInfo(textureFilename, info);

		if (availResolutions & (unsigned)ResolutionMask::k1080p)
		{
			const bool omitThisResolution = (m_resolutions == libdb2::Gui2Resolutions::k1440p);
			if (!omitThisResolution)
			{
				TextureMetaData& metadata = CreateOneTextureGenerationData(AddFilePathSuffix(textureFilename, "_1080p"), textureSpecList, listOfMetaData, info, kTexIdNone, kTexIdNone);

				id1080p = metadata.m_id[(int)Resolution::kCanonical];
				metadata.m_id[(int)Resolution::kCanonical] = kTexIdNone;
				metadata.m_id[(int)Resolution::k1080p] = id1080p;
			}
		}
		if (availResolutions & (unsigned)ResolutionMask::k1440p)
		{
			const bool omitThisResolution = (m_resolutions == libdb2::Gui2Resolutions::k1080p);
			if (!omitThisResolution)
			{
				TextureMetaData& metadata = CreateOneTextureGenerationData(AddFilePathSuffix(textureFilename, "_1440p"), textureSpecList, listOfMetaData, info, kTexIdNone, kTexIdNone);

				id1440p = metadata.m_id[(int)Resolution::kCanonical];
				metadata.m_id[(int)Resolution::kCanonical] = kTexIdNone;
				metadata.m_id[(int)Resolution::k1440p] = id1440p;
			}
		}
		if (availResolutions & (unsigned)ResolutionMask::kCanonical)
		{
			const bool omit2160p = (0U != (availResolutions & (unsigned)ResolutionMask::k1080p))
			                    && (0U != (availResolutions & (unsigned)ResolutionMask::k1440p));
			//ALWAYS_ASSERT(!omit2160p || (id1440p.Value() != kTexIdNone.Value() && id1080p.Value() != kTexIdNone.Value())); // no longer true b/c we can omit certain resolutions

			TextureMetaData& metadata = CreateOneTextureGenerationData(textureFilename, textureSpecList, listOfMetaData, info, id1440p, id1080p, omit2160p);

			if (false)//if (omit2160p)
			{
				INOTE("0x%016llX, 0x%016llX, 0x%016llX: %s\n", metadata.m_id[0].Value(), metadata.m_id[1].Value(), metadata.m_id[2].Value(), textureFilename.c_str());
			}
		}
	}

	return true;
}

bool BuildTransformUITextures::PopulateOnePreEvalDependency(int inputIndex, const std::string& absoluteFilename)
{
	std::string::size_type extension = FindFileExtension(absoluteFilename);
	std::string ext = (extension != std::string::npos) ? absoluteFilename.substr(extension + 1) : "png";

	std::stringstream indexStream;
	indexStream << ext.c_str() << inputIndex;
	BuildPath buildPath(absoluteFilename);
	int64_t timestamp = FileIO::getFileModTime(absoluteFilename.c_str());
	if (timestamp != 0)
	{
		m_preEvaluateDependencies.SetInputFilenameAndTimeStamp(indexStream.str(), buildPath.AsPrefixedPath(), timestamp);
		return true;
	}
	//else
	//{
	//	m_preEvaluateDependencies.AddMissingInputFile(indexStream.str(), buildPath.AsPrefixedPath());
	//}

	return false;
}

void BuildTransformUITextures::PopulatePreEvalDependencies(BuildSpec& buildSpec)
{
	std::string rootPath = GetRootGui2Path();

	/* old way
	std::string searchPath = GetRootTexturesPath();
	std::set<std::string>& pngSet;
	std::string excludeBuildFilePath;
	GatherTexturesFromList(m_buildFile, searchPath, pngSet, &m_verbose, &excludeBuildFilePath);

	if (!excludeBuildFilePath.empty())
	{
		// Don't duplicate textures that will be included in the "common" texture dictionary (as specified by build-common.txt).
		// (NOTE: The name of this file is extracted from an !exclude line in the build-*.txt file scanned above.)
		std::set<std::string> excludePngSet;
		GatherTexturesFromList(excludeBuildFilePath.c_str(), searchPath, excludePngSet);

		std::set<std::string> diff;
		std::set_difference(pngSet.begin(), pngSet.end(), excludePngSet.begin(), excludePngSet.end(),
			std::inserter(diff, diff.begin()));

		if (diff.empty())
		{
			// it's entirely possible that one of the multiplayer modes uses no unique textures...
			// if so, just build a pak file containing a single texture that's known to exist
			std::string fallbackPng = searchPath + "general-textures/blur-background.png";
			std::set<std::string>::const_iterator it = excludePngSet.find(fallbackPng);
			if (it != excludePngSet.end())
				diff.insert(fallbackPng);
		}

		pngSet.swap(diff);
	}
	*/

	if (!buildSpec.IsExtension("png"))
		IABORT("Build file %s doesn't include a line of the form !extension .png\n", buildSpec.GetInitFileFullPath());
	buildSpec.AddExcludePattern("_1080p.");
	buildSpec.AddExcludePattern("_1440p.");
	buildSpec.Gather(rootPath);

	m_verbose = buildSpec.IsVerbose();
	const std::set<std::string>& pngSet = buildSpec.GetFullPaths();

	int inputIndex = 0;
	for (const std::string& pngFullPath : pngSet)
	{
		const bool     existsCanonical     = PopulateOnePreEvalDependency(inputIndex, pngFullPath);
		const unsigned existsCanonicalMask = existsCanonical ? (unsigned)ResolutionMask::kCanonical : 0U;
		inputIndex +=  existsCanonical ? 1 : 0;

		const bool     exists1440p     = PopulateOnePreEvalDependency(inputIndex, AddFilePathSuffix(pngFullPath, "_1440p"));
		const unsigned exists1440pMask = exists1440p ? (unsigned)ResolutionMask::k1440p : 0U;
		inputIndex +=  exists1440p ? 1 : 0;

		const bool     exists1080p     = PopulateOnePreEvalDependency(inputIndex, AddFilePathSuffix(pngFullPath, "_1080p"));
		const unsigned exists1080pMask = exists1080p ? (unsigned)ResolutionMask::k1080p : 0U;
		inputIndex +=  exists1080p ? 1 : 0;

		const unsigned availResolutions = (existsCanonicalMask | exists1440pMask | exists1080pMask);

		if (availResolutions != 0U)
		{
			std::string jsonFullPath = GetTextureInfoFullPath(pngFullPath);
			const bool jsonExists = PopulateOnePreEvalDependency(inputIndex, jsonFullPath);
			inputIndex +=  jsonExists ? 1 : 0;

			m_pngList.push_back(TexturePair(pngFullPath, availResolutions));
		}
		else
		{
			INOTE("Skipping %s -- file does not exist.\n", pngFullPath.c_str());
		}
	}

	if (!m_pngList.empty())
	{
		std::sort(m_pngList.begin(), m_pngList.end(),
			[](const TexturePair& a, const TexturePair& b)
			{
				return (a.first < b.first);
			});

		if (m_verbose)
		{
			std::string logPath = rootPath + "build-log-textures.txt";
			FILE* pfLog = fopen(logPath.c_str(), "a");

			for (auto p : m_pngList)
			{
				const std::string relPath = p.first.substr(rootPath.size());
				std::string availRes = "2160p";
				if (p.second & (unsigned)ResolutionMask::k1440p)
					availRes += ",1440p";
				if (p.second & (unsigned)ResolutionMask::k1080p)
					availRes += ",1080p";

				char logLine[256];
				snprintf(logLine, sizeof(logLine), "%s (%s)\n", relPath.c_str(), availRes.c_str());
				INOTE(logLine);
				if (pfLog)
					fwrite(logLine, 1, strlen(logLine), pfLog);
			}

			if (pfLog)
				fclose(pfLog);
		}
	}
}

BuildTransformStatus BuildTransformUITextures::Evaluate()
{
	std::string rootPath = GetRootGui2Path();
	INOTE("\nBuilding GUI2 textures as specified in %s/...\n\n", m_buildSpecFilePath.c_str());

	if (m_pngList.empty())
	{
		IERR("The list of textures is empty. Make sure this is in your perforce workspace: //ndi/dev/t2/data/gui2/... //%s/c:/t2/data/gui2/...", m_toolParams->m_userName.c_str());
		return BuildTransformStatus::kFailed;
	}

	TextureGenerationData textureSpecList;
	std::vector<TextureMetaData> listOfMetaData;
	CreateTextureGenerationData(m_pngList, textureSpecList, listOfMetaData);

	WriteOutputs(textureSpecList, listOfMetaData);

	return BuildTransformStatus::kOutputsUpdated;
}

bool BuildTransformUITextures::WriteOutputs(TextureGenerationData& textureSpecList, const std::vector<TextureMetaData>& listOfMetaData) const
{
	//serialize the array of texture spec
	NdbStream texturesListStream;
	textureSpecList.WriteToNdbStream(texturesListStream);

	const BuildPath& texturesListPath = GetOutputPath("textureslist");
	DataStore::WriteData(texturesListPath, texturesListStream);

	//write a bo file containing a map between the file path to texture hash.
	BigStreamWriter streamWriter(m_toolParams->m_streamConfig);
	WriteOutputs_MetaData(listOfMetaData, streamWriter);
	NdiBoWriter boWriter(streamWriter);
	boWriter.Write();

	const BuildPath& textureMetadataPath = GetOutputPath("texturesuimetadata");
	DataStore::WriteData(textureMetadataPath, boWriter.GetMemoryStream());

	return true;
}

bool BuildTransformUITextures::WriteOutputs_MetaData(const std::vector<TextureMetaData>& listOfMetaData, BigStreamWriter& writer) const
{
	BigStreamWriter::Item* item = writer.StartItem(BigWriter::UI_TEXTURES_METADATA);

	writer.Write8U(listOfMetaData.size());

	std::vector<ICETOOLS::Location> pointersToString;

	//write the metadata
	for (const TextureMetaData& data : listOfMetaData)	
	{
		ICETOOLS::Location pathPtr = writer.WritePointer();
		pointersToString.push_back(pathPtr);

		for (int i = 0; i < (int)Resolution::kCount; ++i)
			writer.Write8U(data.m_id[i].Value());
		writer.Write4U(data.m_originalWidth);
		writer.Write4U(data.m_originalHeight);
		writer.WriteF(data.m_uv[0]);
		writer.WriteF(data.m_uv[1]);
		writer.WriteF(data.m_uv[2]);
		writer.WriteF(data.m_uv[3]);
	}

	//write the contiguous strings and set the pointers in the map.
	int index = 0;
	for (const TextureMetaData& data : listOfMetaData)
	{
		writer.SetPointer(pointersToString[index]);
		writer.WriteString(data.m_path);
		++index;
	}

	writer.EndItem();

	writer.AddLoginItem(item, BigWriter::UI_TEXTURES_METADATA_PRIORITY);

	return true;
}

void BuildModuleUITextures_Configure(const BuildTransformContext *const pContext,
									const libdb2::Actor& actor,
									std::string& textureBoFilename,
									std::string& textureBoLowFilename,
									std::string& textureUIMetadataFilename,
									BuildSpec& buildSpec, libdb2::Gui2Resolutions::EResolution resolutions)
{
	std::string buildPath;
	toolsutils::GetCommonBuildPathNew(buildPath, pContext->m_toolParams.m_local);
	
	BuildTransformUITextures* uiTextureTransform = new BuildTransformUITextures(&pContext->m_toolParams, buildSpec, resolutions);
	std::string uiTexturesListFilename = buildPath + BUILD_TRANSFORM_UI_TEXTURES + FileIO::separator + actor.Name() + FileIO::separator + "texturespeclist.ndb";
	TransformOutput uiTextureListPath(uiTexturesListFilename, "textureslist");

	textureUIMetadataFilename = buildPath + BUILD_TRANSFORM_UI_TEXTURES + FileIO::separator + actor.Name() + FileIO::separator + "textureuimetadata.bo";
	TransformOutput uiTexturesMapIdPath(textureUIMetadataFilename, "texturesuimetadata");

	uiTextureTransform->SetInput(TransformInput(actor.DiskPath()));

	std::vector<TransformOutput> outputs;
	outputs.push_back(uiTextureListPath);
	outputs.push_back(uiTexturesMapIdPath);
	uiTextureTransform->SetOutputs(outputs);
	pContext->m_buildScheduler.AddBuildTransform(uiTextureTransform, pContext);

	// --------------------------------------- Texture build --------------------------------------- 
	// TextureReadSpec
	const std::string consolidateInputFilename = pContext->m_toolParams.m_buildPathTextureReadSpec + actor.Name() + FileIO::separator + "textureslist.a";
	const std::string spawnerOutputPath = pContext->m_toolParams.m_buildPathTextureReadSpec;
	const std::string buildOutputPath = pContext->m_toolParams.m_buildPathTextureBuild;
	TextureReadSpec *const texReadSpecTransform = new TextureReadSpec(pContext, pContext->m_toolParams, spawnerOutputPath, buildOutputPath, consolidateInputFilename);
	
	// TextureReadSpec - inputs
	texReadSpecTransform->SetInput(uiTextureListPath);
	std::vector<TransformOutput> texReadSpecOutputs;

	// TextureReadSpec - outputs
	const TransformOutput dummyOutput(pContext->m_toolParams.m_buildPathTextureReadSpec + actor.Name() + FileIO::separator + "dummy.txt");
	//const TransformOutput texReadSpecOutputListOutput = TransformOutput(pContext->m_toolParams.m_buildPathTextureReadSpec + actor.Name() + FileIO::separator + "outputlist.ndb", "OutputList");
	const TransformOutput texReadSpecSpawnListOutput = TransformOutput(pContext->m_toolParams.m_buildPathTextureReadSpec + actor.Name() + FileIO::separator + "spawnlist.ndb", "SpawnList");
	texReadSpecOutputs.push_back(dummyOutput);
	//texReadSpecOutputs.push_back(texReadSpecOutputListOutput);
	texReadSpecOutputs.push_back(texReadSpecSpawnListOutput);
	texReadSpecTransform->SetOutputs(texReadSpecOutputs);

	pContext->m_buildScheduler.AddBuildTransform(texReadSpecTransform, pContext);

	// BuildTransformSpawner
	BuildTransformSpawner *const pSpawnerTransform = new BuildTransformSpawner(*pContext);

	// BuildTransformSpawner - inputs
	pSpawnerTransform->SetInput(texReadSpecSpawnListOutput);

	// BuildTransformSpawner - outputs 
	pSpawnerTransform->SetOutput(BuildTransformSpawner::CreateOutputPath(texReadSpecSpawnListOutput));

	pContext->m_buildScheduler.AddBuildTransform(pSpawnerTransform, pContext);
	// --------------------------------------- Texture build --------------------------------------- 

	BuildTransform_ConsolidateAndBuildTextures *const pConsolidateAndBuildTexs = new BuildTransform_ConsolidateAndBuildTextures(pContext, 
																												pContext->m_toolParams, 
																												false, 
																												actor.Name());

	const std::string boTextureDirname = pContext->m_toolParams.m_buildPathConsolidateTextures + actor.FullName() + FileIO::separator;

	textureBoFilename = boTextureDirname + "texture.bo";
	textureBoLowFilename = boTextureDirname + "texture.bo.low";
	std::string boTextureCacheFilename = textureBoFilename + ".cache";
	std::string boTextureCacheTxtFilename = boTextureCacheFilename + ".txt";
	std::string textureHashMapFilename = boTextureDirname + "textureSpecHashMap.ndb";
	std::vector<TransformOutput> outputFiles = {
	//	{ textureBoFilename, "TexturesBo" }, // we don't have any mipmaps, so don't write out the high mips
		{ textureBoLowFilename, "TexturesBoLow" },
		{ boTextureCacheFilename, "TexturesBoCache", TransformOutput::kReplicate }, // Replicate for: livetexture
		{ boTextureCacheTxtFilename, "TexturesBoCacheTxt", TransformOutput::kReplicate }, // Replicate for: livetexture
		{ textureHashMapFilename, "textureSpecHashMap" }
	};

	pConsolidateAndBuildTexs->SetInput(TransformInput(consolidateInputFilename));
	pConsolidateAndBuildTexs->SetOutputs(outputFiles);

	pContext->m_buildScheduler.AddBuildTransform(pConsolidateAndBuildTexs, pContext);
}