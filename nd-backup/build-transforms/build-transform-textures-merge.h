#pragma once

#include "build-transform.h"

class TexturesMergeTransform : public BuildTransform
{
public:

	TexturesMergeTransform();
	~TexturesMergeTransform();

	BuildTransformStatus Evaluate() override;
};