#include <windows.h>
#include <string>

static int SpawnProcessAndCaptureOutput(std::string& consoleOutput, const std::string& commandLine)
{
	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = NULL;

	STARTUPINFO si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_FORCEOFFFEEDBACK | STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
	si.wShowWindow = SW_HIDE;

	PROCESS_INFORMATION pi;
	ZeroMemory(&pi, sizeof(pi));

	// Create the pipe
	HANDLE hReadPipe = NULL, hWritePipe = NULL;
	if(!CreatePipe(&hReadPipe, &hWritePipe, &sa, 128 * 1024))
		return FALSE;

	si.hStdOutput = hWritePipe;
	si.hStdError = hWritePipe;
	si.hStdInput = NULL;

	// Create the process
	if(!CreateProcess(NULL, (LPSTR)commandLine.c_str(), &sa, &sa, TRUE, 0, NULL, NULL, &si, &pi))
		{
		CloseHandle(hReadPipe);
		CloseHandle(hWritePipe);
		return FALSE;
	}

	// Wait for completion
	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	// Finish output
	CloseHandle(hWritePipe);

	// Deliver all output
	char chBuffer[0x200];
	DWORD bytesRead = 0;
	// NOTE: there's always a byte available beyond the buffer
	while(ReadFile(hReadPipe, chBuffer, sizeof(chBuffer) - 1, &bytesRead, NULL)
		  && (int(bytesRead) > 0))
		consoleOutput+=std::string(chBuffer, bytesRead);

	CloseHandle(hReadPipe);

	return 0;
}

bool FileIsPerforceControlled(const std::string& fullyQualifiedLocalFileName)
{
	std::string output;
	std::string command = (std::string("p4 fstat -m1 ") + fullyQualifiedLocalFileName).c_str();
	SpawnProcessAndCaptureOutput(output, command);
	return  (output.find("no such file(s).") == std::string::npos);
}
