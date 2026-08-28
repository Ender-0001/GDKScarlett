#define GDKS_TRACE_TAG "gdks"
#include "Common.h"
#include "ImportPatch.h"

#include <winternl.h>
#include <vector>

#pragma comment(lib, "ntdll.lib")

static const char* GPrefix = "gdks_";

struct XboxDll
{
	const char* importName;
	const wchar_t* layerFile;
	bool reimplemented;
};

static const XboxDll GXboxDlls[] =
{
	{ "d3d12_x.dll",      L"gdks_d3d12_x.dll",  true },
	{ "d3d12_xs.dll",     L"gdks_d3d12_xs.dll", true },
	{ "xmem.dll",         L"gdks_xmem.dll",     true },
	{ "PIXEvt.dll",       L"gdks_PIXEvt.dll",   true },
	{ "AcpHal.dll",       L"gdks_AcpHal.dll",   true },
	{ "mfplat.dll",       L"gdks_mfplat.dll",   true },
	{ "xg_x.dll",         L"xg_x.dll",          false },
	{ "xg_xs.dll",        L"xg_xs.dll",          false },
	{ "dxcompiler_x.dll", L"dxcompiler_x.dll",  false },
};

static bool FileExists(const std::wstring& path)
{
	return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool ReadMemory(HANDLE process, ULONGLONG address, void* destination, SIZE_T size)
{
	SIZE_T bytesRead = 0;
	return ReadProcessMemory(process, (LPCVOID)address, destination, size, &bytesRead) && bytesRead == size;
}

static bool WriteMemory(HANDLE process, ULONGLONG address, const void* source, SIZE_T size)
{
	SIZE_T bytesWritten = 0;
	return WriteProcessMemory(process, (LPVOID)address, source, size, &bytesWritten) && bytesWritten == size;
}

// True when both paths name the same file on disk: same volume and same file id.
static bool SameFile(const std::wstring& a, const std::wstring& b)
{
	HANDLE ha = CreateFileW(a.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	                        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (ha == INVALID_HANDLE_VALUE)
	{
		return false;
	}
	HANDLE hb = CreateFileW(b.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	                        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (hb == INVALID_HANDLE_VALUE)
	{
		CloseHandle(ha);
		return false;
	}
	BY_HANDLE_FILE_INFORMATION ia{};
	BY_HANDLE_FILE_INFORMATION ib{};
	bool same = GetFileInformationByHandle(ha, &ia) && GetFileInformationByHandle(hb, &ib) &&
	            ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber &&
	            ia.nFileIndexHigh == ib.nFileIndexHigh && ia.nFileIndexLow == ib.nFileIndexLow;
	CloseHandle(ha);
	CloseHandle(hb);
	return same;
}

void EnsureLayerAliases(const std::wstring& layerDirectory)
{
	for (const XboxDll& dll : GXboxDlls)
	{
		if (!dll.reimplemented)
		{
			continue;
		}
		std::wstring target = layerDirectory + L"\\" + dll.layerFile;
		if (!FileExists(target))
		{
			continue;
		}

		int length = MultiByteToWideChar(CP_ACP, 0, dll.importName, -1, nullptr, 0);
		std::wstring original(length ? length - 1 : 0, L'\0');
		MultiByteToWideChar(CP_ACP, 0, dll.importName, -1, original.data(), length);
		std::wstring alias = layerDirectory + L"\\" + original;

		if (FileExists(alias))
		{
			if (!SameFile(alias, target))
			{
				LOGF("alias %ls already exists and is a different file; left alone", original.c_str());
			}
			continue;
		}

		if (CreateHardLinkW(alias.c_str(), target.c_str(), nullptr))
		{
			LOGF("alias: %ls -> %ls (hard link)", original.c_str(), dll.layerFile);
			continue;
		}

		DWORD error = GetLastError();
		if (CopyFileW(target.c_str(), alias.c_str(), TRUE))
		{
			LOGF("alias: %ls -> %ls (copied; hard link failed %lu)", original.c_str(), dll.layerFile, error);
			continue;
		}
		LOGF("could not create alias %ls (%lu)", original.c_str(), GetLastError());
	}
}

static bool PatchDelayLoadImports(HANDLE process, ULONGLONG imageBase,
                                  const IMAGE_NT_HEADERS64& ntHeaders)
{
	const IMAGE_DATA_DIRECTORY& directory =
		ntHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
	if (!directory.VirtualAddress || !directory.Size)
	{
		return true;
	}

	std::vector<IMAGE_DELAYLOAD_DESCRIPTOR> descriptors;
	for (SIZE_T i = 0;; ++i)
	{
		IMAGE_DELAYLOAD_DESCRIPTOR descriptor{};
		ULONGLONG address = imageBase + directory.VirtualAddress + i * sizeof(descriptor);
		if (!ReadMemory(process, address, &descriptor, sizeof(descriptor)))
		{
			break;
		}
		if (!descriptor.DllNameRVA)
		{
			break;
		}
		descriptors.push_back(descriptor);
	}
	if (descriptors.empty())
	{
		return true;
	}

	std::vector<size_t> renamed;
	std::vector<std::string> prefixedNames;
	for (size_t i = 0; i < descriptors.size(); ++i)
	{
		if (!descriptors[i].Attributes.RvaBased)
		{
			LOGF("delay-load descriptor %zu is address-based; not patched", i);
			continue;
		}
		char name[128]{};
		if (!ReadMemory(process, imageBase + descriptors[i].DllNameRVA, name, sizeof(name) - 1))
		{
			continue;
		}
		for (const XboxDll& dll : GXboxDlls)
		{
			if (_stricmp(name, dll.importName) != 0 || !dll.reimplemented)
			{
				continue;
			}
			renamed.push_back(i);
			prefixedNames.push_back(std::string(GPrefix) + name);
			LOGF("rename delay import: %s -> %s%s", name, GPrefix, name);
			break;
		}
	}
	if (renamed.empty())
	{
		return true;
	}

	SIZE_T nameTableBytes = 0;
	for (const std::string& name : prefixedNames)
	{
		nameTableBytes += name.size() + 1;
	}

	ULONGLONG blockAddress = 0;
	ULONGLONG candidate = (imageBase + ntHeaders.OptionalHeader.SizeOfImage + 0xFFFF) & ~0xFFFFull;
	for (; candidate < imageBase + 0x7F000000ull; candidate += 0x10000)
	{
		LPVOID allocation = VirtualAllocEx(process, (LPVOID)candidate, nameTableBytes,
		                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (allocation)
		{
			blockAddress = (ULONGLONG)allocation;
			break;
		}
	}
	if (!blockAddress)
	{
		LOGF("could not allocate a block for delay-load names");
		return false;
	}

	ULONGLONG nameAddress = blockAddress;
	ULONGLONG nameRva = blockAddress - imageBase;
	for (size_t i = 0; i < renamed.size(); ++i)
	{
		const std::string& name = prefixedNames[i];
		if (!WriteMemory(process, nameAddress, name.c_str(), name.size() + 1))
		{
			return false;
		}

		ULONGLONG fieldAddress = imageBase + directory.VirtualAddress +
		                         renamed[i] * sizeof(IMAGE_DELAYLOAD_DESCRIPTOR) +
		                         offsetof(IMAGE_DELAYLOAD_DESCRIPTOR, DllNameRVA);
		DWORD rva = (DWORD)nameRva;
		DWORD previousProtection = 0;
		if (!VirtualProtectEx(process, (LPVOID)fieldAddress, sizeof(rva), PAGE_READWRITE, &previousProtection))
		{
			LOGF("VirtualProtectEx on delay-load descriptor failed (%lu)", GetLastError());
			return false;
		}
		bool written = WriteMemory(process, fieldAddress, &rva, sizeof(rva));
		DWORD restoredProtection = 0;
		VirtualProtectEx(process, (LPVOID)fieldAddress, sizeof(rva), previousProtection, &restoredProtection);
		if (!written)
		{
			LOGF("could not patch delay-load name RVA");
			return false;
		}

		nameAddress += name.size() + 1;
		nameRva += (ULONGLONG)name.size() + 1;
	}

	LOGF("delay-load imports patched (%zu renamed)", renamed.size());
	return true;
}

bool PatchImports(HANDLE process, const std::wstring& layerDirectory, const std::wstring& gameDirectory)
{
	PROCESS_BASIC_INFORMATION processInfo{};
	ULONG returnLength = 0;
	if (NtQueryInformationProcess(process, ProcessBasicInformation, &processInfo, sizeof(processInfo), &returnLength) != 0)
	{
		LOGF("could not query process information");
		return false;
	}

	ULONGLONG imageBase = 0;
	if (!ReadMemory(process, (ULONGLONG)processInfo.PebBaseAddress + 0x10, &imageBase, sizeof(imageBase)) || !imageBase)
	{
		LOGF("could not read image base");
		return false;
	}

	IMAGE_DOS_HEADER dosHeader{};
	if (!ReadMemory(process, imageBase, &dosHeader, sizeof(dosHeader)) || dosHeader.e_magic != IMAGE_DOS_SIGNATURE)
	{
		LOGF("bad DOS header");
		return false;
	}

	IMAGE_NT_HEADERS64 ntHeaders{};
	if (!ReadMemory(process, imageBase + dosHeader.e_lfanew, &ntHeaders, sizeof(ntHeaders)) ||
	    ntHeaders.Signature != IMAGE_NT_SIGNATURE)
	{
		LOGF("bad NT header");
		return false;
	}
	if (ntHeaders.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
	{
		LOGF("target is not PE32+ (x64)");
		return false;
	}

	DWORD importDirectoryRva = ntHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
	if (!importDirectoryRva)
	{
		LOGF("no import directory");
		return false;
	}

	std::vector<IMAGE_IMPORT_DESCRIPTOR> descriptors;
	for (DWORD i = 0; ; ++i)
	{
		IMAGE_IMPORT_DESCRIPTOR descriptor{};
		if (!ReadMemory(process, imageBase + importDirectoryRva + i * sizeof(descriptor), &descriptor, sizeof(descriptor)))
		{
			return false;
		}
		descriptors.push_back(descriptor);
		if (descriptor.Name == 0 && descriptor.FirstThunk == 0)
		{
			break;
		}
	}

	std::vector<std::string> prefixedNames;
	std::vector<size_t> renamedDescriptors;
	std::vector<std::wstring> missing;
	for (size_t i = 0; i + 1 < descriptors.size(); ++i)
	{
		if (!descriptors[i].Name)
		{
			continue;
		}
		char importName[128]{};
		if (!ReadMemory(process, imageBase + descriptors[i].Name, importName, sizeof(importName) - 1))
		{
			return false;
		}
		for (const XboxDll& dll : GXboxDlls)
		{
			if (_stricmp(importName, dll.importName) != 0)
			{
				continue;
			}
			if (!FileExists(layerDirectory + L"\\" + dll.layerFile) &&
			    !FileExists(gameDirectory + L"\\" + dll.layerFile))
			{
				missing.push_back(dll.layerFile);
			}
			if (dll.reimplemented)
			{
				renamedDescriptors.push_back(i);
				prefixedNames.push_back(std::string(GPrefix) + importName);
				LOGF("rename import: %s -> %s%s", importName, GPrefix, importName);
			}
			break;
		}
	}
	if (!missing.empty())
	{
		for (const std::wstring& name : missing)
		{
			LOGF("MISSING: %ls (not in layer or game dir)", name.c_str());
		}
		LOGF("layer is incomplete; aborting");
		return false;
	}
	if (!PatchDelayLoadImports(process, imageBase, ntHeaders))
	{
		return false;
	}

	if (renamedDescriptors.empty())
	{
		LOGF("no target imports found (already patched or wrong exe?)");
		return true;
	}

	SIZE_T descriptorTableBytes = descriptors.size() * sizeof(IMAGE_IMPORT_DESCRIPTOR);
	SIZE_T nameTableBytes = 0;
	for (const std::string& name : prefixedNames)
	{
		nameTableBytes += name.size() + 1;
	}

	ULONGLONG blockAddress = 0;
	ULONGLONG candidate = (imageBase + ntHeaders.OptionalHeader.SizeOfImage + 0xFFFF) & ~0xFFFFull;
	for (; candidate < imageBase + 0x7F000000ull; candidate += 0x10000)
	{
		LPVOID allocation = VirtualAllocEx(process, (LPVOID)candidate, descriptorTableBytes + nameTableBytes,
		                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (allocation)
		{
			blockAddress = (ULONGLONG)allocation;
			break;
		}
	}
	if (!blockAddress)
	{
		LOGF("could not allocate an RVA-reachable block near the image");
		return false;
	}
	ULONGLONG blockRva = blockAddress - imageBase;

	ULONGLONG nameAddress = blockAddress + descriptorTableBytes;
	ULONGLONG nameRva = blockRva + descriptorTableBytes;
	for (size_t i = 0; i < renamedDescriptors.size(); ++i)
	{
		const std::string& name = prefixedNames[i];
		descriptors[renamedDescriptors[i]].Name = (DWORD)nameRva;
		if (!WriteMemory(process, nameAddress, name.c_str(), name.size() + 1))
		{
			return false;
		}
		nameAddress += name.size() + 1;
		nameRva += (ULONGLONG)name.size() + 1;
	}
	if (!WriteMemory(process, blockAddress, descriptors.data(), descriptorTableBytes))
	{
		return false;
	}

	ULONGLONG importDirectoryAddress = imageBase + dosHeader.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
	                                   offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory) +
	                                   IMAGE_DIRECTORY_ENTRY_IMPORT * sizeof(IMAGE_DATA_DIRECTORY);
	IMAGE_DATA_DIRECTORY importDirectory{};
	importDirectory.VirtualAddress = (DWORD)blockRva;
	importDirectory.Size = (DWORD)descriptorTableBytes;

	DWORD previousProtection = 0;
	if (!VirtualProtectEx(process, (LPVOID)importDirectoryAddress, sizeof(importDirectory), PAGE_READWRITE, &previousProtection))
	{
		LOGF("VirtualProtectEx on data directory failed (%lu)", GetLastError());
		return false;
	}
	bool repointed = WriteMemory(process, importDirectoryAddress, &importDirectory, sizeof(importDirectory));
	DWORD restoredProtection = 0;
	VirtualProtectEx(process, (LPVOID)importDirectoryAddress, sizeof(importDirectory), previousProtection, &restoredProtection);
	if (!repointed)
	{
		LOGF("could not repoint import directory");
		return false;
	}

	ULONGLONG boundDirectoryAddress = imageBase + dosHeader.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
	                                  offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory) +
	                                  IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT * sizeof(IMAGE_DATA_DIRECTORY);
	IMAGE_DATA_DIRECTORY clearedBoundDirectory{};
	DWORD previousBoundProtection = 0;
	if (VirtualProtectEx(process, (LPVOID)boundDirectoryAddress, sizeof(clearedBoundDirectory), PAGE_READWRITE, &previousBoundProtection))
	{
		WriteMemory(process, boundDirectoryAddress, &clearedBoundDirectory, sizeof(clearedBoundDirectory));
		DWORD restoredBoundProtection = 0;
		VirtualProtectEx(process, (LPVOID)boundDirectoryAddress, sizeof(clearedBoundDirectory), previousBoundProtection, &restoredBoundProtection);
	}

	LOGF("import table repointed to 0x%08lX (%zu renamed)", (unsigned long)blockRva, renamedDescriptors.size());
	return true;
}

void InjectXGameRuntime(HANDLE process, const std::wstring& layerDirectory)
{
	std::wstring dllPath = layerDirectory + L"\\XGameRuntime.dll";

	if (!FileExists(dllPath))
	{
		LOGF("XGameRuntime.dll not found: %ls", dllPath.c_str());
		return;
	}

	SIZE_T pathSize = (dllPath.size() + 1) * sizeof(wchar_t);

	LPVOID remotePath = VirtualAllocEx(
		process,
		nullptr,
		pathSize,
		MEM_COMMIT | MEM_RESERVE,
		PAGE_READWRITE
	);

	if (!remotePath)
	{
		LOGF("VirtualAllocEx failed for XGameRuntime.dll (%lu)", GetLastError());
		return;
	}

	if (!WriteMemory(process, (ULONGLONG)remotePath, dllPath.c_str(), pathSize))
	{
		LOGF("could not write XGameRuntime.dll path");
		VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
		return;
	}

	HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
	if (!kernel32)
	{
		LOGF("could not get kernel32.dll");
		VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
		return;
	}

	FARPROC loadLibraryW = GetProcAddress(kernel32, "LoadLibraryW");
	if (!loadLibraryW)
	{
		LOGF("could not resolve LoadLibraryW");
		VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
		return;
	}

	HANDLE thread = CreateRemoteThread(
		process,
		nullptr,
		0,
		(LPTHREAD_START_ROUTINE)loadLibraryW,
		remotePath,
		0,
		nullptr
	);

	if (!thread)
	{
		LOGF("CreateRemoteThread failed for XGameRuntime.dll (%lu)", GetLastError());
		VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
		return;
	}

	WaitForSingleObject(thread, INFINITE);

	DWORD exitCode = 0;
	if (!GetExitCodeThread(thread, &exitCode))
	{
		LOGF("could not get XGameRuntime.dll load result (%lu)", GetLastError());
	}
	else if (exitCode == 0)
	{
		LOGF("LoadLibraryW failed for XGameRuntime.dll");
	}

	CloseHandle(thread);
	VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
}