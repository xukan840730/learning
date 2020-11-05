#pragma once

#include <map>

#include "build-transform.h"

class ToolParams;

class BuildTransform_SoundBank : public BuildTransform
{
public:
	BuildTransform_SoundBank(const BuildTransformContext* pContext, const std::string& soundBankName);
	~BuildTransform_SoundBank();

	BuildTransformStatus Evaluate() override;

private:

	bool CopyFileToBAM(const std::string& sourceFilePath);
	bool PatchSoundBank(const std::string& bankPath, std::string &xmlBuffer, bool errorsEncountered);

	std::string m_soundBankName;
	bool m_strictEvaluation;

	static std::map<std::string, std::string> m_allSourceFilesT2;		// Original filename -> normalized full path of found file
	static std::map<std::string, std::string> m_allSourceFilesT1;		// Original filename -> normalized full path of found file
	static std::map<std::string, std::string> m_allSourceFilesBig4;		// Original filename -> normalized full path of found file
	static std::map<std::string, std::string> m_allSourceFilesBig3;		// Original filename -> normalized full path of found file
	static std::map<std::string, std::string> m_allSourceFilesBig2;		// Original filename -> normalized full path of found file
	static std::map<std::string, std::string> m_allSourceFilesBig1;		// Original filename -> normalized full path of found file

	std::vector<std::pair<std::string, std::string>> m_filesRemapped;	// Original full path -> normalized full path

	static FileDateCache m_fdc;
};


Err ScheduleSoundBank(const ToolParams& toolParams, const std::string& assetName, BuildTransformContext* pContext);