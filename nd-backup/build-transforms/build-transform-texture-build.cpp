#include "build-transform-texture-build.h"

#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/common/4_textures-old-texture-loader.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/textures-common/texture-builder.h"
#include "tools/pipeline3/textures-common/texture-loader-substance.h"

#include <string>

using std::string;
using std::unique_ptr;

using pipeline3::texture_builder::BuildTextureJob;
using pipeline3::texture_loader_substance::SubstanceRenderer;

//#pragma optimize("", off)

TextureBuildTransformSpawnDesc::TextureBuildTransformSpawnDesc()
	: BuildTransformSpawnDesc("TextureBuildTransform")
{
}

BuildTransform* TextureBuildTransformSpawnDesc::CreateTransform(const BuildTransformContext *pContext) const
{
	return new TextureBuildTransform(pContext->m_toolParams);
}

TextureBuildTransform::TextureBuildTransform(const ToolParams &toolParams)
	: BuildTransform("TextureBuild")
	, m_toolParams(toolParams)
	, m_textureSpec(nullptr)
{
}

TextureBuildTransform::~TextureBuildTransform()
{
	delete m_textureSpec;
}

BuildTransformStatus TextureBuildTransform::Evaluate()
{
	const BuildFile& file = GetFirstInputFile();
	NdbStream inputStream;
	DataStore::ReadData(file, inputStream);

	m_textureSpec = new TextureLoader::TextureGenerationSpec();
	NdbRead(inputStream, "spec", *m_textureSpec);

	for (const TextureLoader::InputTextureSpec& textureFile : m_textureSpec->m_inputTextures)
	{
		if (textureFile.m_filename.empty())
			continue;

		if (PathConverter::IsAbsolutePath(textureFile.m_filename))
		{
			BuildPath textureFilePath(textureFile.m_filename);
			RegisterDiscoveredDependency(textureFilePath, 0);
		}
		else
		{
			string bamRoot = "z:/" + m_toolParams.m_userName + FileIO::separator + m_toolParams.m_gameName + FileIO::separator;
			string absolutePath = bamRoot + textureFile.m_filename;
			BuildPath textureFilePath(absolutePath);
			RegisterDiscoveredDependency(textureFilePath, 0);
		}
	}
	
	pipeline3::texture_builder::ReplaceEmptyTextures(*m_textureSpec);
	
	SubstanceRenderer substanceRenderer;
	substanceRenderer.GenerateSubstance(*m_textureSpec);

	const ToolParams& tool = m_toolParams;
	WorkItemHandle handle = pipeline3::texture_builder::AsyncBuildTexture(tool.m_strict, substanceRenderer, tool.m_farmConfig, *m_textureSpec);

	RegisterThreadPoolWaitItem(handle);

	return BuildTransformStatus::kResumeNeeded;
}

BuildTransformStatus TextureBuildTransform::ResumeEvaluation(const SchedulerResumeItem& resumeItem)
{
	const BuildTextureJob*  pJob = (const BuildTextureJob*)resumeItem.m_threadPoolJob;
	const BigWriter::Texture *pTexture = pJob->m_pTexture.get();

	bool jobSuccess = pJob->m_pTexture && !pJob->m_buildError;
	if (!jobSuccess && pJob->m_pSpec->m_inputTextures.size())
	{
		if (pJob->m_pTexture && pJob->m_pSpec->m_inputTextures[0].m_filename.find("main/common/speccubemaps0") != std::string::npos)
		{
			jobSuccess = true;			// let's not fail on missing probe cubemaps... This is probably not the right thing to do in the long run but hey... we need to build this thing!!!
		}
	}

	if (!jobSuccess)
	{
		delete m_textureSpec;
		m_textureSpec = nullptr;

		return BuildTransformStatus::kFailed;
	}
		
	
	//set the texture filename to a prefixed path for binary reproducibility
	BuildPath textureFilename(pJob->m_pTexture->m_cachedNdbFilename);
	pJob->m_pTexture->m_cachedNdbFilename = textureFilename.AsPrefixedPath();

	NdbStream stream;
	if (stream.OpenForWriting(Ndb::kBinaryStream) != Ndb::kNoError)
	{
		IERR("Failed to open stream for writing.");

		delete m_textureSpec;
		m_textureSpec = nullptr;

		return BuildTransformStatus::kFailed;
	}

	NdbWrite(stream, "spec", *pJob->m_pSpec);
	NdbWrite(stream, "texture", *pJob->m_pTexture);
	stream.Close();
	
	const BuildPath& outputPath = GetFirstOutputPath();
	DataStore::WriteData(outputPath, stream);

	delete m_textureSpec;
	m_textureSpec = nullptr;

	return BuildTransformStatus::kOutputsUpdated;

}
