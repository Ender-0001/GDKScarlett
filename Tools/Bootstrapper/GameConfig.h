#pragma once
#include <string>

struct PackageInfo
{
	std::wstring executable;
	std::wstring applicationId;
	std::wstring identityName;
	std::wstring titleId;
};

bool ParseGameConfig(const std::wstring& configPath, PackageInfo& info);
