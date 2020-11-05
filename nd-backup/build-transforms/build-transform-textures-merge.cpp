#include "build-transform-textures-merge.h"

#include "tools/pipeline3/common/4_textures-old-texture-loader.h"
#include "tools/pipeline3/common/blobs/data-store.h"

#include <set>
#include <string>

using std::set;
using std::string;

TexturesMergeTransform::TexturesMergeTransform()
	:BuildTransform("TexturesMerge")
{}

TexturesMergeTransform::~TexturesMergeTransform()
{}

BuildTransformStatus TexturesMergeTransform::Evaluate()
{
	TextureGenerationData output;

	set<string> uniqueSpecs;
	for (const TransformInput& input : GetInputs())
	{
		NdbStream tgdStream;
		DataStore::ReadData(input.m_file, tgdStream);

		TextureGenerationData tgdInput;
		tgdInput.ReadFromNdbStream(tgdStream);

		for (const auto& texentry : tgdInput.m_textureSpec)
		{
			if (uniqueSpecs.find(texentry.m_cachedFilename) == uniqueSpecs.end())
			{
				uniqueSpecs.insert(texentry.m_cachedFilename);
				output.m_textureSpec.push_back(texentry);
			}
		}
	}

	NdbStream stream;
	output.WriteToNdbStream(stream);

	const BuildPath& outputPath = GetFirstOutputPath();
	DataStore::WriteData(outputPath, stream);

	return BuildTransformStatus::kOutputsUpdated;
}