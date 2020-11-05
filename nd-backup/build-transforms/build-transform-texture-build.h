#pragma once

#include "build-transform.h"

#include "tools/pipeline3/build/tool-params.h"
#include "tools/pipeline3/build/build-transforms/build-transform-spawner.h"

namespace pipeline3
{
	namespace texture_loader_substance
	{
		class SubstanceRenderer;
	}
}

namespace TextureLoader
{
	struct TextureGenerationSpec;
}

struct TextureBuildTransformSpawnDesc : public BuildTransformSpawnDesc
{
	TextureBuildTransformSpawnDesc();
	virtual BuildTransform *CreateTransform(const BuildTransformContext *pContext) const override;
};

class TextureBuildTransform : public BuildTransform
{
public:
	TextureBuildTransform(const ToolParams &tool);
	~TextureBuildTransform();

	BuildTransformStatus Evaluate() override;
	BuildTransformStatus ResumeEvaluation(const SchedulerResumeItem& resumeItem) override;

private:

	const ToolParams &m_toolParams;

	TextureLoader::TextureGenerationSpec* m_textureSpec;

};
