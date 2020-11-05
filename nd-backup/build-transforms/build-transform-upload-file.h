#pragma once

#include "build-transform.h"


class BuildTransform_UploadFile : public BuildTransform
{
public:
	BuildTransform_UploadFile();
	~BuildTransform_UploadFile();

	BuildTransformStatus Evaluate() override;
};


