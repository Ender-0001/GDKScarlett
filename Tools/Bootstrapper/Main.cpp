#define GDKS_TRACE_TAG "gdks"
#include "Common.h"
#include "DebugOutput.h"
#include "GameConfig.h"
#include "ImportPatch.h"

#include <objbase.h>
#include <stdio.h>
#include <string>
#include <vector>

static std::wstring DirectoryOf(const std::wstring& path)
{
	size_t separator = path.find_last_of(L"\\/");
	return separator == std::wstring::npos ? std::wstring(L".") : path.substr(0, separator);
}

int wmain(int argc, wchar_t** argv)
{
	if (argc < 2)
	{
		printf("usage: gdks <package-root> [--layer <dir>] [-- <game args>]\n");
		printf("  <package-root> holds MicrosoftGame.config; the exe and G:\\ root come from it.\n");
		printf("  --layer defaults to the current directory.\n");
		return 2;
	}

	std::wstring packageRoot = argv[1];
	while (!packageRoot.empty() && (packageRoot.back() == L'\\' || packageRoot.back() == L'/'))
	{
		packageRoot.pop_back();
	}

	std::wstring layerDirectory;
	std::wstring gameArguments;
	for (int i = 2; i < argc; ++i)
	{
		if (wcscmp(argv[i], L"--layer") == 0 && i + 1 < argc)
		{
			layerDirectory = argv[++i];
		}
		else if (wcscmp(argv[i], L"--") == 0)
		{
			for (int j = i + 1; j < argc; ++j)
			{
				gameArguments += L' ';
				gameArguments += argv[j];
			}
			break;
		}
	}
	if (layerDirectory.empty())
	{
		wchar_t currentDirectory[MAX_PATH]{};
		if (GetCurrentDirectoryW(MAX_PATH, currentDirectory))
		{
			layerDirectory = currentDirectory;
		}
	}

	if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
	{
		LOGF("could not initialize COM");
		return 1;
	}

	PackageInfo package;
	if (!ParseGameConfig(packageRoot + L"\\MicrosoftGame.config", package))
	{
		CoUninitialize();
		return 1;
	}
	std::wstring gamePath = packageRoot + L"\\" + package.executable;
	LOGF("package: identity=%ls app=%ls titleId=%ls",
	     package.identityName.c_str(), package.applicationId.c_str(), package.titleId.c_str());
	LOGF("exe: %ls", gamePath.c_str());

	SetEnvironmentVariableW(L"GDKS_G", packageRoot.c_str());

	DWORD pathLength = GetEnvironmentVariableW(L"PATH", nullptr, 0);
	std::wstring existingPath;
	if (pathLength)
	{
		existingPath.resize(pathLength);
		GetEnvironmentVariableW(L"PATH", existingPath.data(), pathLength);
		if (!existingPath.empty() && existingPath.back() == L'\0')
		{
			existingPath.pop_back();
		}
	}
	SetEnvironmentVariableW(L"PATH", (layerDirectory + L";" + existingPath).c_str());
	LOGF("layer dir on PATH: %ls", layerDirectory.c_str());

	EnsureLayerAliases(layerDirectory);

	std::wstring commandLine = L"\"" + gamePath + L"\"" + gameArguments;
	std::vector<wchar_t> commandLineBuffer(commandLine.begin(), commandLine.end());
	commandLineBuffer.push_back(0);

	STARTUPINFOW startupInfo{};
	startupInfo.cb = sizeof(startupInfo);
	PROCESS_INFORMATION processInfo{};
	std::wstring workingDirectory = DirectoryOf(gamePath);
	if (!CreateProcessW(gamePath.c_str(), commandLineBuffer.data(), nullptr, nullptr, FALSE,
	                    CREATE_SUSPENDED, nullptr, workingDirectory.c_str(), &startupInfo, &processInfo))
	{
		LOGF("CreateProcess failed (%lu): %ls", GetLastError(), gamePath.c_str());
		CoUninitialize();
		return 1;
	}
	LOGF("spawned suspended pid=%lu", processInfo.dwProcessId);

	StartDebugOutputRelay(processInfo.dwProcessId);

	if (!PatchImports(processInfo.hProcess, layerDirectory, workingDirectory))
	{
		LOGF("import patch failed; terminating");
		TerminateProcess(processInfo.hProcess, 1);
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
		CoUninitialize();
		return 1;
	}

	InjectXGameRuntime(processInfo.hProcess, layerDirectory);

	ResumeThread(processInfo.hThread);
	LOGF("resumed; waiting for exit");
	WaitForSingleObject(processInfo.hProcess, INFINITE);

	StopDebugOutputRelay();

	DWORD exitCode = 0;
	GetExitCodeProcess(processInfo.hProcess, &exitCode);
	LOGF("game exited with code %lu (0x%08lX)", exitCode, exitCode);
	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);
	CoUninitialize();
	return (int)exitCode;
}
