#pragma once

#include "build-transform.h"

#include "tools/pipeline3/common/4_textures-old-texture-loader.h"
#include "tools/pipeline3/textures-common/loaded-textureId.h"
#include "tools/libs/libdb2/db2-actor.h"

#include <set>
#include <tuple>
#include <vector>
#include <string>

class BigStreamWriter;
class BuildTransformContext;
class ToolParams;
class BuildSpec;

namespace libdb2
{
	class Actor;
}

class BuildTransformUITextures : public BuildTransform
{
public:
	BuildTransformUITextures(const ToolParams* tool, BuildSpec& buildSpec, libdb2::Gui2Resolutions::EResolution resolutions);
	~BuildTransformUITextures();

	virtual BuildTransformStatus Evaluate() override;

private:
	enum class Resolution
	{
		kCanonical,
		k1440p,
		k1080p,
		// IMPORTANT: if you add any resolutions to this enum you MUST bump the BUILD_TRANSFORM_UI_TEXTURES bucket number, and adjust the engine to match!
		kCount,
	};
	enum class ResolutionMask
	{
		// NOTE: this enum is only used internally, not by the engine, so mask bits can be freely changed (BUT see comment in enum class Resolution)
		kCanonical = 1U,
		k1440p = 2U,
		k1080p = 4U,
	};
	enum class Mode
	{
		kUnspecified,
		kClamp,
		kTile,
	};

	struct TextureInfo
	{
		std::string					m_path;
		TextureLoader::OutputMode	m_format = TextureLoader::kOutputRgba8888;
		Mode						m_mode = Mode::kUnspecified;
		bool						m_compress = false;
	};

	struct TextureMetaData
	{
		std::string m_path;
		pipeline3::LoadedTextureId m_id[Resolution::kCount];
		uint32_t m_originalWidth;
		uint32_t m_originalHeight;
		float m_uv[4];						// min u, min v, max u, max v

		TextureMetaData();
	};

	typedef std::pair<std::string, unsigned> TexturePair;

	const ToolParams* m_toolParams;
	std::string m_buildSpecFilePath;
	std::vector<TexturePair> m_pngList;
	libdb2::Gui2Resolutions::EResolution m_resolutions;
	bool m_compress;
	bool m_verbose;

	void PopulatePreEvalDependencies(BuildSpec& buildSpec);

	std::string GetRootGui2Path() const;
	std::string GetRootWidgetsPath() const;
	std::string GetRootTexturesPath() const;
	std::string GetTextureInfoFullPath(const std::string& textureFullPath) const;
	void LoadTextureInfo(const std::string& textureRelPath, TextureInfo& info) const;
	void GatherTexture(const std::string& pngFullPath, const std::string& limitToContentsOfFolder, std::set<std::string>& pngSet) const;
	static std::string AddFilePathSuffix(const std::string& filePath, const std::string& suffix);
	bool PopulateOnePreEvalDependency(int inputIndex, const std::string& absoluteFilename);
	TextureMetaData& CreateOneTextureGenerationData(const std::string& textureFilename, TextureGenerationData& textureSpecList, std::vector<TextureMetaData>& listOfMetaData,
	                                                const TextureInfo& info, pipeline3::LoadedTextureId id1440p, pipeline3::LoadedTextureId id1080p, const bool omitTextureSpec = false) const;
	bool CreateTextureGenerationData(const std::vector<TexturePair>& pngList, TextureGenerationData& textureSpecList, std::vector<TextureMetaData>& listOfMetaData) const;
	bool WriteOutputs(TextureGenerationData& textureSpecList, const std::vector<TextureMetaData>& listOfMetaData) const;
	bool WriteOutputs_MetaData(const std::vector<TextureMetaData>& listOfMetaData, BigStreamWriter& writer) const;
};

void BuildModuleUITextures_Configure(const BuildTransformContext *const pContext, 
									const libdb2::Actor& actor, 
									std::string& textureBoFilename, 
									std::string& textureBoLowFilename, 
									std::string& textureUIMetadataFilename, 
									BuildSpec& buildSpec, libdb2::Gui2Resolutions::EResolution resolutions);