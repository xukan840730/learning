#include "build-transform-collect-textures.h"

#include "icelib/itscene/ndbwriter.h"

#include "tools/libs/libdb2/db2-actor.h"
#include "tools/libs/libdb2/db2-level.h"
#include "tools/libs/toolsutil/constify.h"
#include "tools/libs/toolsutil/strfunc.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/util/dependency-database-manager.h"
#include "tools/pipeline3/common/4_textures.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/lightmap-injection.h"

#include <memory>
#include <string>
#include <vector>

using namespace pipeline3;
using std::string;
using std::vector;

//#pragma optimize("", off)

BuildTransform_CollectTexturesFromMaterials::BuildTransform_CollectTexturesFromMaterials(const BuildTransformContext *const pContext
																						, bool lightmapsOverrideEnabled)
																						: BuildTransform("CollectTexFromMats", pContext)
{
	m_preEvaluateDependencies.SetString("levelName", "");
	m_preEvaluateDependencies.SetBool("lightmapsOverrideEnabled", lightmapsOverrideEnabled);
}

BuildTransform_CollectTexturesFromMaterials::BuildTransform_CollectTexturesFromMaterials(const BuildTransformContext *const pContext
																						, const std::string& levelName)
																						: BuildTransform("CollectTexFromMats", pContext)
{
	m_preEvaluateDependencies.SetString("levelName", levelName);
}

BuildTransformStatus BuildTransform_CollectTexturesFromMaterials::Evaluate()
{
	const std::string& levelName = m_preEvaluateDependencies.GetValue("levelName");

	LightmapTextures lmapTex;
	if (!levelName.empty())
	{
		DataStore::ReadSerializedData(GetInputFile("LightmapList"), lmapTex);
	}
	else if (DoesInputExist("LightmapList"))
	{
		DataStore::ReadSerializedData(GetInputFile("LightmapList"), lmapTex);
	}

	ITSCENE::ConstCgfxShaderList allShaders;
	std::vector<int> allShadersToEffects;
	std::vector<material_compiler::CompiledEffect> allCompiledEffects;
	const BuildFile buildFile = GetFirstInputFile();
	{
		NdbStream stream;
		DataStore::ReadData(buildFile, stream);

		NdbRead(stream, "m_actorShaders", allShaders);
		NdbRead(stream, "m_actorShadersToEffects", allShadersToEffects);
		NdbRead(stream, "m_actorCompiledEffects", allCompiledEffects);
	}

	TextureGenerationDataEx tgd;
	vector<vector<SamplerPatch>> samplerPatches;
	pipeline3::GatherShadersTextures(allShaders, allShadersToEffects, allCompiledEffects, tgd.m_textureSpec, samplerPatches);

	if (!levelName.empty())
	{
		const libdb2::Level *const pDbLevel = libdb2::GetLevel(levelName);
		if (!pDbLevel->Loaded())
		{
//			delete pDbLevel;
			IABORT("Unknown level %s\n", levelName.c_str());
		}

		AddAlternateTextures(tgd, pDbLevel);
		InjectLightmaps(allShadersToEffects, allCompiledEffects, m_pContext->m_toolParams, lmapTex, &tgd, samplerPatches);

//		delete pDbLevel;
	}
	else
	{
		const bool lightmapsOverrideEnabled = m_preEvaluateDependencies.GetBool("lightmapsOverrideEnabled");
		if (lightmapsOverrideEnabled)
		{
			InjectLightmaps(allShadersToEffects, allCompiledEffects, m_pContext->m_toolParams, lmapTex, &tgd, samplerPatches);
		}
	}

	if (DoesInputExist("LightTextures"))
	{
		NdbStream lightTexturesNdb;
		DataStore::ReadData(GetInputFile("LightTextures"), lightTexturesNdb);

		std::vector<LightBoTexture> lightTextures;
		NdbRead(lightTexturesNdb, "m_lightTextures", lightTextures);

		if (!lightTextures.empty())
		{
			InjectLightTextures(tgd, lightTextures);
		}

		NdbStream lightTexturesIndexedNdb;
		lightTexturesIndexedNdb.OpenForWriting(Ndb::kBinaryStream);
		NdbWrite(lightTexturesIndexedNdb, "m_lightTextures", lightTextures);
		lightTexturesIndexedNdb.Close();
		DataStore::WriteData(GetOutputPath("LightTexturesIndexed"), lightTexturesIndexedNdb);
	}

	{
		TextureGenerationData texGenData;
		INOTE_VERBOSE("---START TEXTURES LIST---");
		for (TextureLoader::TextureGenerationSpecEx& specEx : tgd.m_textureSpec)
		{
			TextureLoader::TextureGenerationSpec& spec = static_cast<TextureLoader::TextureGenerationSpecEx&>(specEx);
			texGenData.m_textureSpec.push_back(spec);
			INOTE_VERBOSE("%s", spec.m_cachedFilename.c_str());
			for (const TextureLoader::InputTextureSpec& inputTexture : spec.m_inputTextures)
			{
				INOTE_VERBOSE("   %s", inputTexture.m_filename.c_str());
			}
		}
		INOTE_VERBOSE("---END TEXTURES LIST---");

		NdbStream stream;
		texGenData.WriteToNdbStream(stream);
		DataStore::WriteData(GetOutputPath("TextureGenNdb"), stream);
	}

	{
		NdbStream patchStream;
		Ndb::ErrorType err = patchStream.OpenForWriting(Ndb::kBinaryStream);
		if (err == Ndb::kNoError)
		{
			NdbWrite(patchStream, "samplerPatchList", samplerPatches);
		}
		else
		{
			IERR("Failed to open for writing ndb stream for sampler patch list. Error %d", err);
			return BuildTransformStatus::kFailed;
		}

		patchStream.Close();

		DataStore::WriteData(GetOutputPath("SamplerPatchListNdb"), patchStream);
	}

	return BuildTransformStatus::kOutputsUpdated;
}


void BuildTransform_CollectTexturesFromMaterials::AddAlternateTextures(TextureGenerationDataEx& tgd, 
																	const libdb2::Level *const pLevel)
{
	vector<string> alternateResourcesTextures;
	pipeline3::GatherAlternateResourcesTextures(pLevel->m_alternateResources.m_textures, *m_pContext->m_toolParams.m_matdb, alternateResourcesTextures);
	for (auto filename : alternateResourcesTextures)
	{
		tgd.m_textureSpec.push_back(TextureLoader::TextureGenerationSpecEx());
		std::string actualFilename;
		toolsutils::Left(actualFilename, filename, ".texture.xml");

		TextureLoader::TextureGenerationSpecEx& textureSpec = tgd.m_textureSpec.back();
		if (pipeline3::GenerateTexGenDataFromTexturename(textureSpec, *m_pContext->m_toolParams.m_matdb, actualFilename))
		{
			if (!textureSpec.m_textureXmlFile.empty())
			{
				m_assetDeps.AddAssetDependency(textureSpec.m_textureXmlFile.c_str(), pLevel->DiskPath().c_str(), "texturespec");

				for (const TextureLoader::InputTextureSpec& textureFile : textureSpec.m_inputTextures)
				{
					if (!textureFile.m_filename.empty())
					{
						const std::string texturePrefixedPath = PathPrefix::BAM + textureFile.m_filename;
						m_assetDeps.AddAssetDependency(texturePrefixedPath.c_str(), textureSpec.m_textureXmlFile.c_str(), "texture");
					}
				}
			}
		}
		else
		{
			tgd.m_textureSpec.pop_back();
		}
	}
}
