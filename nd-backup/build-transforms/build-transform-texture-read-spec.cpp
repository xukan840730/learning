#include "build-transform-texture-read-spec.h"

#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-file-list.h"
#include "tools/pipeline3/common/4_textures-old-texture-loader.h"
#include "tools/pipeline3/common/blobs/data-store.h"

#include "build-transform-texture-build.h"

using std::set;
using std::string;
using std::vector;

// #pragma optimize("", off)

TextureReadSpec::TextureReadSpec(const BuildTransformContext* pContext, const ToolParams &tool, const std::string &spawnerOutputPath, const std::string &buildOutputPath, const std::string& textureBoTransformInputFilename)
	: BuildTransform("TextureReadSpec", pContext)
	, m_toolParams(tool)
	, m_texBuildSpawnerPath(spawnerOutputPath)
	, m_texBuildPath(buildOutputPath)
	, m_textureBoTransformInputFilename(textureBoTransformInputFilename)
{
	SetDependencyMode(BuildTransform::DependencyMode::kIgnoreDependency);	// must be kIgnoreDependency because it calls AddOutput during execution

	m_preEvaluateDependencies.SetConfigInt("SpawnListVersion", TransformSpawnList::Version());
}

TextureReadSpec::~TextureReadSpec()
{
}

BuildTransformStatus TextureReadSpec::Evaluate()
{
	// Read input TextureGenerationData
	const BuildFile& inputFile = GetFirstInputFile();
	NdbStream inputStream;
	DataStore::ReadData(inputFile, inputStream);
	TextureGenerationData texGenerationData;
	texGenerationData.ReadFromNdbStream(inputStream);

	// spawnList includes:
	// 1. 0 to n TextureBuildTransform
	// 2. 1 BuildTransform_FileList
	TransformSpawnList spawnList;
	vector<string> texturePaths;						// 0 to n texture.ndb

	set<string> uniqueTextures;
	for (const TextureLoader::TextureGenerationSpec& spec : texGenerationData.m_textureSpec)
	{
		const size_t pos = spec.m_cachedFilename.find_last_of('/');
		const string textureHash = spec.m_cachedFilename.substr(pos + 1);
		set<string>::const_iterator it = uniqueTextures.find(textureHash);
		if (it != uniqueTextures.cend())
		{
			continue;
		}

		uniqueTextures.insert(textureHash);

		//Serialize texturespec
		NdbStream texSpecStream;
		if (texSpecStream.OpenForWriting(Ndb::kBinaryStream) != Ndb::kNoError)
		{
			IERR("Fail to open stream for writing.");
			return BuildTransformStatus::kFailed;
		}
		NdbWrite(texSpecStream, "spec", spec);
		texSpecStream.Close();

		// Write this texturespec.ndb to the data store
		const string texSpecPath = m_texBuildSpawnerPath + textureHash + FileIO::separator + "texturespec.ndb";
		const BuildPath texSpecBuildPath = BuildPath(texSpecPath);
		DataHash texSpecDataHash;
		DataStore::WriteData(texSpecBuildPath, texSpecStream, &texSpecDataHash);
		AddOutput(TransformOutput(texSpecBuildPath));

		// Spawn TextureBuildTransform
		TextureBuildTransformSpawnDesc *const pTexBulidSpawnDesc = new TextureBuildTransformSpawnDesc();

		// TextureBuildTransform - inputs
		const TransformOutput texSpecOutput = TransformOutput(texSpecBuildPath);
		pTexBulidSpawnDesc->m_inputs = { texSpecOutput };

		// TextureBuildTransform - outputs
		const string texturePath = m_texBuildPath + textureHash + FileIO::separator + "texture.ndb";
		TransformOutput textureOutput(texturePath);
		pTexBulidSpawnDesc->m_outputs = { textureOutput };

		spawnList.AddTransform(pTexBulidSpawnDesc);

		// BuildTransform_FileList input
		texturePaths.push_back(texturePath);
	}

	// Spawn BuildTransform_FileListSpawn
	const TransformOutput dummyOutput = GetFirstOutput();
	const std::string dummyOutputPath = dummyOutput.m_path.AsAbsolutePath();
	texturePaths.push_back(dummyOutputPath);				// Include the dummy output in the file list

	BuildTransform_FileListSpawnDesc *const pFileListSpawnDesc = CreateFileListSpawnDesc(texturePaths, m_textureBoTransformInputFilename);
	spawnList.AddTransform(pFileListSpawnDesc);

	// Write the dummy output
	DataStore::WriteData(dummyOutputPath, "");

	// Write the spawn list
	spawnList.WriteSpawnList(GetOutputPath("SpawnList"));

	return BuildTransformStatus::kOutputsUpdated;
}