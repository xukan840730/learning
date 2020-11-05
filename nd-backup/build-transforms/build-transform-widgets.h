#pragma once

#include "build-transform.h"

namespace libdb2 { class Actor; }
class ToolParams;
class BuildSpec;

class BuildTransform_JsonToDomBo : public BuildTransform
{
public:
	BuildTransform_JsonToDomBo(const BuildTransformContext* pContext);
	~BuildTransform_JsonToDomBo();

	BuildTransformStatus Evaluate() override;

private:
	std::string m_widgetPath;
};


void BuildModuleWidgets_Configure(	const BuildTransformContext *const pContext, 
									const libdb2::Actor& actor, 
									std::vector<std::string>& widgetBos,
									BuildSpec& buildSpec );