#include "tools/pipeline3/build/build-transforms/build-transform-upload-file.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/blobs/blob-cache.h"


//#pragma optimize("", off) // uncomment when debugging in release mode

//--------------------------------------------------------------------------------------------------------------------//
BuildTransform_UploadFile::BuildTransform_UploadFile()
	: BuildTransform("UploadFile")
{
}


//--------------------------------------------------------------------------------------------------------------------//
BuildTransform_UploadFile::~BuildTransform_UploadFile()
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
		0, NULL);

	std::string message = std::string(pszMessage);
	LocalFree(pszMessage);

	message = message.substr(0, message.length() - 2);
	return message;
}


//--------------------------------------------------------------------------------------------------------------------//
BuildTransformStatus BuildTransform_UploadFile::Evaluate()
{
	const BuildFile& inputFile = GetFirstInputFile();
	const std::string inputFilePath = inputFile.AsAbsolutePath();

	HANDLE handle = CreateFile(inputFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, 0);
	if (handle == INVALID_HANDLE_VALUE)
	{
		unsigned int errorCode = GetLastError();
		fprintf(stderr, "ERROR: Unable to file [%s]: \n%s (0x%x)\n", inputFilePath.c_str(), GetErrorString(GetLastError()).c_str(), errorCode);
		return BuildTransformStatus::kFailed;
	}

	DWORD dataSize = GetFileSize(handle, NULL);

	char* pBuffer = new char[dataSize + 2];
	DWORD bytesRead = 0;
	ReadFile(handle, pBuffer, dataSize, &bytesRead, NULL);
	pBuffer[dataSize] = 0;
	pBuffer[dataSize + 1] = 0;
	CloseHandle(handle);

	const BuildPath& outputPath = GetFirstOutputPath();
	OutputBlob blob = OutputBlob(pBuffer, dataSize);
	DataStore::WriteData(outputPath, blob);

	delete[] pBuffer;

	return BuildTransformStatus::kOutputsUpdated;
}
