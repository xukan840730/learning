#include "tools/pipeline3/build/build-transforms/build-transform-sound-bank.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-upload-file.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/path-converter.h"
#include "tools/pipeline3/toolversion.h"
#include "common/util/timer.h"

#define LIBXML_STATIC
#include "tools/libs/libxml2/include/libxml/parser.h"

//#pragma optimize("", off) // uncomment when debugging in release mode


std::map<std::string, std::string> BuildTransform_SoundBank::m_allSourceFilesT2;		// Original filename -> normalized full path of found file
std::map<std::string, std::string> BuildTransform_SoundBank::m_allSourceFilesT1;		// Original filename -> normalized full path of found file
std::map<std::string, std::string> BuildTransform_SoundBank::m_allSourceFilesBig4;		// Original filename -> normalized full path of found file
std::map<std::string, std::string> BuildTransform_SoundBank::m_allSourceFilesBig3;		// Original filename -> normalized full path of found file
std::map<std::string, std::string> BuildTransform_SoundBank::m_allSourceFilesBig2;		// Original filename -> normalized full path of found file
std::map<std::string, std::string> BuildTransform_SoundBank::m_allSourceFilesBig1;		// Original filename -> normalized full path of found file
FileDateCache BuildTransform_SoundBank::m_fdc;

//--------------------------------------------------------------------------------------------------------------------//
BuildTransform_SoundBank::BuildTransform_SoundBank(const BuildTransformContext* pContext, const std::string& soundBankName)
	: BuildTransform("SoundBank", pContext)
	, m_soundBankName(soundBankName)
{
	SetDependencyMode(DependencyMode::kIgnoreDependency);

	m_strictEvaluation = m_pContext->m_toolParams.m_strict;
}


//--------------------------------------------------------------------------------------------------------------------//
BuildTransform_SoundBank::~BuildTransform_SoundBank()
{
}

//--------------------------------------------------------------------------------------------------------------------//
static std::string GetErrorString(unsigned int errorCode)
{
	LPTSTR pszMessage;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		errorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&pszMessage,
		0, NULL );
		
	std::string message = std::string(pszMessage);
	LocalFree(pszMessage);
		
	message = message.substr(0, message.length()-2);
	return message;
}


//--------------------------------------------------------------------------------------------------------------------//
static bool LoadXmlFile(const std::string& xmlFilePath, xmlDocPtr& document, std::string& outXmlBuffer)
{
	HANDLE handle = CreateFile(xmlFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, 0);
	if (handle == INVALID_HANDLE_VALUE) 
	{
		unsigned int errorCode = GetLastError();
		fprintf(stderr, "ERROR: Unable to access database file [%s]: \n%s (0x%x)\n", xmlFilePath.c_str(), GetErrorString(GetLastError()).c_str(), errorCode);
		return false;
	}

	DWORD length = GetFileSize(handle, NULL);

	char* pBuffer = new char [length + 2];
	DWORD bytesRead;
	ReadFile(handle, pBuffer, length, &bytesRead, NULL);
	pBuffer[length] = 0;
	pBuffer[length+1] = 0;
	CloseHandle(handle);

	document = xmlParseMemory(pBuffer, length);
	outXmlBuffer = pBuffer;
	delete [] pBuffer;

	return true;
}


//--------------------------------------------------------------------------------------------------------------------//
bool BuildTransform_SoundBank::CopyFileToBAM(const std::string& sourceFilePath)
{
	std::string normalizedSourcePath = sourceFilePath;
	normalizedSourcePath = FileIO::toLowercase((char*)normalizedSourcePath.c_str());
	FileIO::convertSlashes((char*)normalizedSourcePath.c_str(), '/');

	// Is it already in BAM?
	if (normalizedSourcePath.find("y:/t2/art/") == 0)
		return true;
	if (normalizedSourcePath.find("z:/" + m_pContext->m_toolParams.m_userName + "/t2/art/") == 0)
		return true;

	// Intonation have had many mappings during the years
	size_t netApp4Pos = normalizedSourcePath.find("//netapp4-dog/");
	if (netApp4Pos == 0)
		normalizedSourcePath = "\\\\netapp2-dog\\" + normalizedSourcePath.substr(14);
	size_t netAppPos = normalizedSourcePath.find("//netapp-dog/");
	if (netAppPos == 0)
		normalizedSourcePath = "\\\\netapp2-dog\\" + normalizedSourcePath.substr(13);
	size_t sDrivePos = normalizedSourcePath.find("s:/");
	if (sDrivePos == 0)
		normalizedSourcePath = "\\\\netapp2-dog\\intonation/" + normalizedSourcePath.substr(3);

	std::string basePath;
	if (normalizedSourcePath.find("\\\\netapp2-dog\\intonation/t1/") == 0)
		basePath = "\\\\netapp2-dog\\intonation/t1/";
	else if (normalizedSourcePath.find("\\\\netapp2-dog\\intonation/t2/") == 0)
		basePath = "\\\\netapp2-dog\\intonation/t2/";
	else if (normalizedSourcePath.find("\\\\netapp2-dog\\intonation/big4/") == 0)
		basePath = "\\\\netapp2-dog\\intonation/big4/";
	else if (normalizedSourcePath.find("\\\\netapp2-dog\\intonation/mp-tbd/") == 0)
		basePath = "\\\\netapp2-dog\\intonation/mp-tbd/";
	else if (normalizedSourcePath.find("\\\\netapp-dog\\intonation/big4/") == 0)
		basePath = "\\\\netapp-dog\\intonation/big4/";
	else if (normalizedSourcePath.find("z:/big3/sound/") == 0)
		basePath = "z:/big3/sound/";
	else if (normalizedSourcePath.find("z:/big2/sound/") == 0)
		basePath = "z:/big2/sound/";
	else if (normalizedSourcePath.find("z:/big/sound/") == 0)
		basePath = "z:/big/sound/";
	else if (normalizedSourcePath.find("z:/t1/sound/") == 0)
		basePath = "z:/t1/sound/";
	else if (normalizedSourcePath.find("z:/") == 0)		// Catch-all
		basePath = "z:/";
	else if (normalizedSourcePath.find("c:/") == 0)		// Catch-all
		basePath = "c:/";
	else
	{
		basePath = "";
// 		(m_strictEvaluation ? IERR : INOTE_VERBOSE)("Can't extract basePath from '%s'", normalizedSourcePath.c_str());
// 		return false;
	}

	std::string destPath = PathConverter::ToAbsolutePath(std::string(PathPrefix::BAM) + "art/sound/" + normalizedSourcePath.substr(basePath.length()));

	// Put everything under 'y:/t2/art/-audiosource-/'
	if (destPath.find("/-audiosource-/") == std::string::npos)
	{
		size_t audioSourcePos = destPath.find("/audio source/");
		if (audioSourcePos != std::string::npos)
		{
			// Replace 'audio source' with '-audiosource-'
			destPath = destPath.substr(0, audioSourcePos) + "/-audiosource-/" + destPath.substr(audioSourcePos + 14);
		}
		else
		{
			destPath = PathConverter::ToAbsolutePath(std::string(PathPrefix::BAM) + "art/sound/-audiosource-/_orphans/" + normalizedSourcePath.substr(basePath.length()));
			bool foundOne = false;
			do
			{
				foundOne = false;
				size_t colonPos = destPath.find(":", 2);
				if (colonPos != std::string::npos)
				{
					destPath[colonPos] = '_';
					foundOne = true;
				}
			} while (foundOne);
		}
	}

	// Replace spaces with dashes
	for (char& c : destPath)
		if (c == ' ')
			c = '-';

	std::string srcPathToCopy = normalizedSourcePath;
	bool fileRemapFound = false;
	// Is the file where we say it should be?
	if (m_fdc.fileExists(normalizedSourcePath.c_str()))
	{
		srcPathToCopy = normalizedSourcePath;
		fileRemapFound = true;
	}
	else if (m_strictEvaluation)
	{
		// Try to locate the source file and copy it to BAM

		// Nope, let's try to find a file with the same name
		std::string searchString = sourceFilePath;
		FileIO::convertSlashes((char*)searchString.c_str(), '/');
		size_t slashPos = searchString.find_last_of('/');
		const std::string filename = sourceFilePath.substr(slashPos + 1);

		if (m_allSourceFilesT2.empty())
		{
 			std::vector<std::string> allowedExtensions = { "wav" };
 			GatherFilesInFolderRecursivelyAsMap(m_allSourceFilesT2, "\\\\NETAPP2-DOG\\Intonation/t2/-audiosource-/", allowedExtensions, m_fdc);
 			GatherFilesInFolderRecursivelyAsMap(m_allSourceFilesT2, "\\\\NETAPP2-DOG\\Intonation/mp-tbd/-audiosource-/", allowedExtensions, m_fdc);
		}
		auto searchIter = m_allSourceFilesT2.find(filename);
		if (searchIter != m_allSourceFilesT2.end())
		{
			srcPathToCopy = searchIter->second;
			fileRemapFound = true;
		}

		if (!fileRemapFound)
		{
			if (m_allSourceFilesT1.empty())
			{
				std::vector<std::string> allowedExtensions = { "wav" };
				GatherFilesInFolderRecursivelyAsMap(m_allSourceFilesT1, "\\\\NETAPP2-DOG\\Intonation/t1/audio source/", allowedExtensions, m_fdc);
				GatherFilesInFolderRecursivelyAsMap(m_allSourceFilesT1, "Z:/t1/sound/", allowedExtensions, m_fdc);
			}
			searchIter = m_allSourceFilesT1.find(filename);
			if (searchIter != m_allSourceFilesT1.end())
			{
				srcPathToCopy = searchIter->second;
				fileRemapFound = true;
			}
		}

		if (!fileRemapFound)
		{
			if (m_allSourceFilesBig4.empty())
			{
				std::vector<std::string> allowedExtensions = { "wav" };
 				GatherFilesInFolderRecursivelyAsMap(m_allSourceFilesBig4, "\\\\NETAPP2-DOG\\Intonation/big4/Audio Source/", allowedExtensions, m_fdc);
 				GatherFilesInFolderRecursivelyAsMap(m_allSourceFilesBig4, "\\\\NETAPP2-DOG\\Intonation/big4/users/Jeremy/transfer/", allowedExtensions, m_fdc);
 				GatherFilesInFolderRecursivelyAsMap(m_allSourceFilesBig4, "\\\\NETAPP2-DOG\\Intonation/big4/users/Neil/U4 Main/Created Sounds/phys/", allowedExtensions, m_fdc);
			}
			searchIter = m_allSourceFilesBig4.find(filename);
			if (searchIter != m_allSourceFilesBig4.end())
			{
				srcPathToCopy = searchIter->second;
				fileRemapFound = true;
			}
		}

		if (!fileRemapFound)
		{
			if (m_allSourceFilesBig3.empty())
			{
				std::vector<std::string> allowedExtensions = { "wav" };
 				GatherFilesInFolderRecursivelyAsMap(m_allSourceFilesBig3, "U:/big3/sound1/foley/", allowedExtensions, m_fdc);
 				GatherFilesInFolderRecursivelyAsMap(m_allSourceFilesBig3, "U:/big3/sound1/from-derrick/", allowedExtensions, m_fdc);
 				GatherFilesInFolderRecursivelyAsMap(m_allSourceFilesBig3, "U:/big3/sound1/U3_levels/", allowedExtensions, m_fdc);
 				GatherFilesInFolderRecursivelyAsMap(m_allSourceFilesBig3, "U:/big3/sound1/bruce-xfer/", allowedExtensions, m_fdc);
			}
			searchIter = m_allSourceFilesBig3.find(filename);
			if (searchIter != m_allSourceFilesBig3.end())
			{
				srcPathToCopy = searchIter->second;
				fileRemapFound = true;
			}
		}

		if (!fileRemapFound)
		{
			if (m_allSourceFilesBig2.empty())
			{
				std::vector<std::string> allowedExtensions = { "wav" };
				GatherFilesInFolderRecursivelyAsMap(m_allSourceFilesBig2, "U:/big2/sound/source/", allowedExtensions, m_fdc);
			}
			searchIter = m_allSourceFilesBig2.find(filename);
			if (searchIter != m_allSourceFilesBig2.end())
			{
				srcPathToCopy = searchIter->second;
				fileRemapFound = true;
			}
		}

		if (!fileRemapFound)
		{
			if (m_allSourceFilesBig1.empty())
			{
				std::vector<std::string> allowedExtensions = { "wav" };
 				GatherFilesInFolderRecursivelyAsMap(m_allSourceFilesBig1, "U:/big/sound/bruce mac - PC/", allowedExtensions, m_fdc);
 				GatherFilesInFolderRecursivelyAsMap(m_allSourceFilesBig1, "U:/big/sound/PaulFox Mac TXFR/GameMastered/", allowedExtensions, m_fdc);
			}
			searchIter = m_allSourceFilesBig1.find(filename);
			if (searchIter != m_allSourceFilesBig1.end())
			{
				srcPathToCopy = searchIter->second;
				fileRemapFound = true;
			}
		}
	}

	if (m_strictEvaluation && fileRemapFound)
	{
		if (m_fdc.fileExists(destPath.c_str()))
		{
			// Make sure the timestamps match
			FileIO::FileTime srcFileTime;
			FileIO::FileTime destFileTime;
			m_fdc.readFileTime(srcPathToCopy.c_str(), srcFileTime);
			m_fdc.readFileTime(destPath.c_str(), destFileTime);

			if (srcFileTime != destFileTime)
			{
				IERR("Source and Dest timestamps don't match for '%s' to '%s'", srcPathToCopy.c_str(), destPath.c_str());
			}
		}
		else
		{
			toolsutils::CreatePath(destPath.c_str());
			FileIO::copyFile(destPath.c_str(), srcPathToCopy.c_str());
			INOTE_VERBOSE("Copying audio source asset from '%s' to '%s'", srcPathToCopy.c_str(), destPath.c_str());
		}
		m_filesRemapped.push_back(std::make_pair(sourceFilePath, PathConverter::ToRelativePath(destPath)));
	}

	return fileRemapFound;
}


//--------------------------------------------------------------------------------------------------------------------//
BuildTransformStatus BuildTransform_SoundBank::Evaluate()
{
	INOTE_VERBOSE("[%.3f] Starting Evaluation", GetSecondsSinceAppStart());

	bool errorsEncountered = false;

	const std::string soundBankBasePath = "c:/" + std::string(NdGetEnv("GAMENAME")) + "/sound/banks";

	INOTE_VERBOSE("[%.3f] Gathering available sound banks", GetSecondsSinceAppStart());
	std::map<std::string, std::string> availableSoundBanks;
	FileDateCache fdc;
	std::vector<std::string> allowedExtensions = { "bank" };
	GatherFilesInFolderRecursivelyAsMap(availableSoundBanks, "c:/" + std::string(NdGetEnv("GAMENAME")) + "/sound/banks", allowedExtensions, fdc);

	auto searchIter = availableSoundBanks.find(m_soundBankName + ".bank");
	if (searchIter == availableSoundBanks.end())
	{
		(m_strictEvaluation ? IERR : INOTE_VERBOSE)("The sound bank '%s' does not have a corresponding .bank file", m_soundBankName.c_str());
		if (m_strictEvaluation)
			errorsEncountered = true;

		const BuildPath& dummyOutput = GetFirstOutputPath();
		DataStore::WriteData(dummyOutput, "");

		return errorsEncountered ? BuildTransformStatus::kFailed : BuildTransformStatus::kOutputsUpdated;
	}

	INOTE_VERBOSE("[%.3f] Parsing sound bank XML", GetSecondsSinceAppStart());
	std::string xmlBuffer;
	xmlDocPtr document;
 	if (!LoadXmlFile(searchIter->second, document, xmlBuffer))
 		return BuildTransformStatus::kFailed;

	std::map<std::string, std::string> dependentWaveFiles;

	INOTE_VERBOSE("[%.3f] Validating sound bank", GetSecondsSinceAppStart());
	const xmlNode* pRoot = document->children;
	const xmlNode* pBlock = pRoot->children;
	while (pBlock)
	{
		if (strcmp((const char*)pBlock->name, "waveforms") == 0)
		{
			const xmlNode* pWaveform = pBlock->children;
			while (pWaveform)
			{
				const xmlNode* pWaveformData = pWaveform->children;

				std::string waveformName;
				while(pWaveformData)
				{
					if (strcmp((const char*)pWaveformData->name, "name") == 0)
					{
						waveformName = (const char*)pWaveformData->children->content;
					}
					if (strcmp((const char*)pWaveformData->name, "source-path") == 0)
					{
						std::string sourceFilePath = (const char*)pWaveformData->children->content;
						std::string normalizedSourceFilePath = FileIO::toLowercase((char*)sourceFilePath.c_str());
						FileIO::convertSlashes((char*)normalizedSourceFilePath.c_str(), '/');

						if (normalizedSourceFilePath.find("y:/") == 0)
						{
							normalizedSourceFilePath = "z:/" + m_pContext->m_toolParams.m_userName + normalizedSourceFilePath.substr(2);
							sourceFilePath = "z:/" + m_pContext->m_toolParams.m_userName + sourceFilePath.substr(2);
						}

						// Make sure that the source data is BAM
						if (normalizedSourceFilePath.find(PathConverter::ToAbsolutePath(PathPrefix::BAM)) != 0)
						{
//    							bool fileExistInBAM = CopyFileToBAM(sourceFilePath);
//   							if (!fileExistInBAM)
							{
								(m_strictEvaluation ? IERR : INOTE_VERBOSE)("Source file '%s' is missing for waveform '%s'!", sourceFilePath.c_str(), waveformName.c_str());
								sourceFilePath = std::string(PathPrefix::BAM) + "MISSING FILE" + sourceFilePath;
								if (m_strictEvaluation)
									errorsEncountered = true;
							}
						}

						// Only track dependent files in strict mode
						if (m_strictEvaluation)
						{
							bool found = false;
							for (const auto &iter : m_filesRemapped)
							{
								if (iter.first == sourceFilePath)
								{
									dependentWaveFiles.insert(std::make_pair(waveformName, std::string(PathPrefix::BAM) + iter.second));
									found = true;
									break;
								}
							}
							if (!found)
							{
								std::string depPath = FileIO::toLowercase((char*)sourceFilePath.c_str());
								if (depPath.find("y:") == 0)
									depPath = "z:/" + m_pContext->m_toolParams.m_userName + sourceFilePath.substr(2);
								FileIO::convertSlashes((char*)depPath.c_str(), '/');
								dependentWaveFiles.insert(std::make_pair(waveformName, PathConverter::ToPrefixedPath(depPath)));
							}
						}
					}
					pWaveformData = pWaveformData->next;
				}

				pWaveform = pWaveform->next;
			}
		}
		else if (strcmp((const char*)pBlock->name, "streams") == 0)
		{
			const xmlNode* pStream = pBlock->children;
			while (pStream)
			{
				const xmlNode* pStreamData = pStream->children;

				std::string streamName;
				while (pStreamData)
				{
					if (strcmp((const char*)pStreamData->name, "name") == 0)
					{
						streamName = (const char*)pStreamData->children->content;
					}
					if (strcmp((const char*)pStreamData->name, "NGS") == 0 ||
						strcmp((const char*)pStreamData->name, "BRB") == 0 || 
						strcmp((const char*)pStreamData->name, "BRB2") == 0 || 
						strcmp((const char*)pStreamData->name, "AndroidOpenSLES") == 0)
					{
						const xmlNode* pExportFormatData = pStreamData->children;
						while (pExportFormatData)
						{
							if (strcmp((const char*)pExportFormatData->name, "source-path") == 0)
							{
								std::string streamFilePath = (const char*)pExportFormatData->children->content;
								std::string normalizedSourceFilePath = FileIO::toLowercase((char*)streamFilePath.c_str());
								FileIO::convertSlashes((char*)normalizedSourceFilePath.c_str(), '/');

								// Make sure that the source data is BAM
// 								if (normalizedSourceFilePath.find("z:/" + m_pContext->m_toolParams.m_gameName + "/build/ps4/main/sound") != 0)
// 								{
// 									(m_strictEvaluation ? IERR : INOTE_VERBOSE)("Streaming file '%s' is missing for stream '%s'!", streamFilePath.c_str(), streamName.c_str());
// 									streamFilePath = std::string(PathPrefix::BAM) + "MISSING FILE" + streamFilePath;
// 									if (m_strictEvaluation)
// 										errorsEncountered = true;
// 								}

								// Only track dependent files in strict mode
								// Let's move the wave conversion over from wavebot to here!
// 								if (m_strictEvaluation)
// 								{
// 									std::string depPath = streamFilePath;
// 									FileIO::convertSlashes((char*)depPath.c_str(), '/');
// 									dependentWaveFiles.insert(std::make_pair(streamName, PathConverter::ToPrefixedPath(depPath)));
// 								}
							}

							pExportFormatData = pExportFormatData->next;
						}
					}
					pStreamData = pStreamData->next;
				}

				pStream = pStream->next;
			}
		}

		pBlock = pBlock->next;
	}
	INOTE_VERBOSE("[%.3f] Validation completed", GetSecondsSinceAppStart());

	if (m_strictEvaluation)
	{
		// Did we remap anything in the bank?
		if (!m_filesRemapped.empty())
		{
			INOTE_VERBOSE("[%.3f] Patching the sound bank", GetSecondsSinceAppStart());
			// Update the .bank XML file itself
			errorsEncountered = PatchSoundBank(searchIter->second, xmlBuffer, errorsEncountered);
		}


		if (!errorsEncountered)
		{
			auto iter = dependentWaveFiles.cbegin();
			while (iter != dependentWaveFiles.cend())
			{
				std::string wavFilePath = iter->second;
				RegisterDiscoveredDependency(BuildPath(wavFilePath), 0);

				iter++;
			}
		}
	}

	if (!errorsEncountered)
	{
		const BuildPath& dummyOutput = GetFirstOutputPath();
		DataStore::WriteData(dummyOutput, "");
	}

	INOTE_VERBOSE("[%.3f] Evaluation completed", GetSecondsSinceAppStart());
	return errorsEncountered ? BuildTransformStatus::kFailed : BuildTransformStatus::kOutputsUpdated;
}

static std::string ConvertAmpersands(const std::string& str)
{
	std::string escapedStr = str;

	size_t scanPos = 0;
	size_t escapeCharPos = 0;
	do
	{
		escapeCharPos = escapedStr.find("&", scanPos);
		if (escapeCharPos != std::string::npos)
		{
			escapedStr = escapedStr.substr(0, escapeCharPos) + "&amp;" + escapedStr.substr(escapeCharPos + 1);
			scanPos = escapeCharPos + 5;
		}
	} while (escapeCharPos != std::string::npos);

	return escapedStr;
}

//--------------------------------------------------------------------------------------------------------------------//
bool BuildTransform_SoundBank::PatchSoundBank(const std::string& bankPath, std::string &xmlBuffer, bool errorsEncountered)
{
	INOTE_VERBOSE("Updating the .bank file '%s'", bankPath.c_str());

	size_t initialScanPos = 0;
	std::string newXmlBuffer;
	newXmlBuffer.reserve(xmlBuffer.size() + m_filesRemapped.size() * 64);
	for (auto iter = m_filesRemapped.begin(); iter != m_filesRemapped.end(); ++iter)
	{
		// Special consideration for escaped characters
		std::string oldSourcePath = ConvertAmpersands(iter->first);

		size_t pos = xmlBuffer.find(oldSourcePath, initialScanPos);
		if (pos == std::string::npos)
			IABORT("A path found in the file could not be found the second time??!?!");

		std::string newSourcePath = ConvertAmpersands("y:/" + m_pContext->m_toolParams.m_gameName + "/" + iter->second);
		FileIO::convertSlashes((char*)newSourcePath.c_str(), '\\');

		INOTE_VERBOSE("Replaced source-path '%s' with '%s'", oldSourcePath.c_str(), newSourcePath.c_str());
		newXmlBuffer += xmlBuffer.substr(initialScanPos, pos - initialScanPos) + newSourcePath.c_str();
		initialScanPos = pos + oldSourcePath.length();
	}
	newXmlBuffer += xmlBuffer.substr(initialScanPos);

	HANDLE handle = CreateFile(bankPath.c_str(), GENERIC_WRITE, NULL, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (handle != INVALID_HANDLE_VALUE)
	{
		DWORD bytesWritten = 0;
		WriteFile(handle, newXmlBuffer.c_str(), newXmlBuffer.length(), &bytesWritten, NULL);
		CloseHandle(handle);
	}
	else
	{
		IERR("Failed updating the .bank file '%s'. Please check it out in Perforce and try again.", bankPath.c_str());
		errorsEncountered = true;
	}
	return errorsEncountered;
}


//--------------------------------------------------------------------------------------------------------------------//
Err ScheduleSoundBank(const ToolParams& toolParams, const std::string& assetName, BuildTransformContext* pContext)
{
	INOTE_VERBOSE("Creating Build Transforms for sound bank %s...", assetName.c_str());

	// Add the sound bank name to the SID database
	StringToStringId64(assetName.c_str(), true);

	// Kick off this sound bank parsing transform to gather information about missing source assets
	BuildTransform_SoundBank* pSoundBankXform = new BuildTransform_SoundBank(pContext, assetName);
	pSoundBankXform->SetOutput(TransformOutput(toolParams.m_buildPathSoundBank + assetName + ".txt", "unnamed", TransformOutput::kIncludeInManifest));
	pContext->m_buildScheduler.AddBuildTransform(pSoundBankXform, pContext);

	// Special 'main-upload' folder to trick the pattern conversion for prefixed paths
	const std::string basePath = "z:/" + std::string(NdGetEnv("GAMENAME")) + "/build/ps4/main-upload/" + BUILD_TRANSFORM_SOUNDBANK_FOLDER;
	const std::string bnkPath = basePath + "/" + assetName + ".bnk";
	const std::string strPath = basePath + "/" + assetName + ".str";

	// Upload the main sound bank
// 	{
// 		BuildTransform_UploadFile* pXform = new BuildTransform_UploadFile();
// 		TransformInput inputFile(bnkPath);
// 		inputFile.m_type = TransformInput::kSourceFile;
// 		pXform->SetInput(inputFile);
// 
// 		// We need to convert this to a proper build output
// 		std::string prefixedPath = PathConverter::ToPrefixedPath(bnkPath);
// 		std::string postPrefix = PathConverter::GetPostPrefix(prefixedPath);
// 		pXform->SetOutput(TransformOutput(PathPrefix::BUILD_OUTPUT + postPrefix));
// 		pContext->m_buildScheduler.AddBuildTransform(pXform, pContext);
// 	}

	// #2
	{
		BuildTransform_UploadFile* pXform = new BuildTransform_UploadFile();
		TransformInput inputFile(bnkPath);
		inputFile.m_type = TransformInput::kSourceFile;
		pXform->SetInput(inputFile);

		// We need to convert this to a proper build output
		std::string prefixedPath = PathConverter::ToPrefixedPath(bnkPath);
		std::string postPrefix = PathConverter::GetPostPrefix(prefixedPath);
 		pXform->SetOutput(TransformOutput(PathPrefix::BUILD_OUTPUT + postPrefix, "unnamed", TransformOutput::kIncludeInManifest));
		pContext->m_buildScheduler.AddBuildTransform(pXform, pContext);
	}

	// Parse the 'str' (streaming) file and upload any referenced xvags from the sfx directory
	if (FileIO::fileExists(strPath.c_str()))
	{
		BuildTransform_UploadFile* pXform = new BuildTransform_UploadFile();
		TransformInput inputFile(strPath);
		inputFile.m_type = TransformInput::kSourceFile;
		pXform->SetInput(inputFile);

		// We need to convert this to a proper build output
		std::string prefixedPath = PathConverter::ToPrefixedPath(strPath);
		std::string postPrefix = PathConverter::GetPostPrefix(prefixedPath);
		pXform->SetOutput(TransformOutput(PathPrefix::BUILD_OUTPUT + postPrefix, "unnamed", TransformOutput::kIncludeInManifest));
		pContext->m_buildScheduler.AddBuildTransform(pXform, pContext);

		std::vector<std::string> lines = FileIO::ReadAllLines(strPath.c_str());
		for (const std::string& xvagPath : lines)
		{
			if (xvagPath.empty())
				continue;

			// We need to convert the 'main' path to 'main-upload'
			std::string uploadXvagPath = FileIO::toLowercase(xvagPath.c_str());
			FileIO::convertSlashes((char*)uploadXvagPath.c_str(), '/');
			size_t mainPos = uploadXvagPath.find("/main/");
			uploadXvagPath.replace(mainPos, 6, "/main-upload/");

			// For the interim, convert any xvag paths that reference "sound1/streams/sfx" to "sfx1".
			// This hack will be removed once the streams are natively built by the tools and the banks
			// are all refreshed to point to the proper stream xvag locations.
			std::string sound1StreamsSfx = "sound1/streams/sfx";
			size_t sound1StreamsSfxPos = uploadXvagPath.find(sound1StreamsSfx);
			if (sound1StreamsSfxPos != std::string::npos)
			{
				uploadXvagPath.replace(sound1StreamsSfxPos, sound1StreamsSfx.length(), BUILD_TRANSFORM_SFX_FOLDER);
			}

			// Ignore if the final path is incorrect.
			if (uploadXvagPath.find(BUILD_TRANSFORM_SFX_FOLDER "/") == std::string::npos)
			{
				continue;
			}

			// Upload the streaming xvag
// 			{
// 				BuildTransform_UploadFile* pXform = new BuildTransform_UploadFile();
// 				TransformInput inputFile(uploadXvagPath);
// 				inputFile.m_type = TransformInput::kSourceFile;
// 				pXform->SetInput(inputFile);
// 
// 				// We need to convert this to a proper build output
// 				std::string prefixedPath = PathConverter::ToPrefixedPath(uploadXvagPath);
// 				std::string postPrefix = PathConverter::GetPostPrefix(prefixedPath);
// 				pXform->SetOutput(TransformOutput(PathPrefix::BUILD_OUTPUT + postPrefix));
// 				pContext->m_buildScheduler.AddBuildTransform(pXform, pContext);
// 			}

			// #2
			{
				BuildTransform_UploadFile* pXform = new BuildTransform_UploadFile();
				TransformInput inputFile(uploadXvagPath);
				inputFile.m_type = TransformInput::kSourceFile;
				pXform->SetInput(inputFile);

				// We need to convert this to a proper build output
				std::string prefixedPath = PathConverter::ToPrefixedPath(uploadXvagPath);
				std::string postPrefix = PathConverter::GetPostPrefix(prefixedPath);
				pXform->SetOutput(TransformOutput(toolParams.m_buildPathSfx + postPrefix.substr(strlen(BUILD_TRANSFORM_SFX_FOLDER "/")), "unnamed", TransformOutput::kIncludeInManifest));
				pContext->m_buildScheduler.AddBuildTransform(pXform, pContext);
			}
		}
	}

	return Err::kOK;
}
