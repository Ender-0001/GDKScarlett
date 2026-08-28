#pragma once
#include <windows.h>
#include <string>

void EnsureLayerAliases(const std::wstring& layerDirectory);
bool PatchImports(HANDLE process, const std::wstring& layerDirectory, const std::wstring& gameDirectory);
void InjectXGameRuntime(HANDLE process, const std::wstring& layerDirectory);
