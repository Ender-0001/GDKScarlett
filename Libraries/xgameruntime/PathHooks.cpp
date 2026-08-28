#include "PathHooks.h"

#include <psapi.h>
#include <strsafe.h>
#include <stdio.h>

#pragma comment(lib, "psapi.lib")

namespace GDKScarlett::XGameRuntime
{
	static HMODULE GSelf = nullptr;
	static wchar_t GGameRootW[MAX_PATH];
	static char GGameRootA[MAX_PATH];
	static wchar_t GLocalAppDataW[MAX_PATH];
	static char GLocalAppDataA[MAX_PATH];
	static wchar_t GDeveloperW[MAX_PATH];
	static char GDeveloperA[MAX_PATH];
	static bool GRedirectRelative = false;

	static bool IsDeviceNameW(const wchar_t* path)
	{
		static const wchar_t* devices[] = { L"CONOUT$", L"CONIN$", L"CON", L"NUL", L"AUX", L"PRN" };
		for (const wchar_t* device : devices)
		{
			if (lstrcmpiW(path, device) == 0)
			{
				return true;
			}
		}
		return false;
	}

	static bool IsDeviceNameA(const char* path)
	{
		static const char* devices[] = { "CONOUT$", "CONIN$", "CON", "NUL", "AUX", "PRN" };
		for (const char* device : devices)
		{
			if (lstrcmpiA(path, device) == 0)
			{
				return true;
			}
		}
		return false;
	}

	static const wchar_t* RedirectW(const wchar_t* input, wchar_t* output, size_t count)
	{
		if (!input || !input[0] || IsDeviceNameW(input))
		{
			return input;
		}

		wchar_t normalized[1024];
		size_t length = 0;
		while (input[length] && length < ARRAYSIZE(normalized) - 1)
		{
			normalized[length] = (input[length] == L'/') ? L'\\' : input[length];
			++length;
		}
		normalized[length] = L'\0';
		if (length < 2)
		{
			return input;
		}

		if (normalized[1] == L':')
		{
			const wchar_t* base = nullptr;
			if (normalized[0] == L'G' || normalized[0] == L'g')
			{
				base = GGameRootW;
			}
			else if (normalized[0] == L'T' || normalized[0] == L't')
			{
				base = GLocalAppDataW;
			}
			else if (normalized[0] == L'D' || normalized[0] == L'd')
			{
				base = GDeveloperW;
			}
			if (!base || !base[0])
			{
				return input;   // a real drive (C:, F:, ...)
			}
			StringCchCopyW(output, count, base);
			StringCchCatW(output, count, normalized + 2);
			return output;
		}
		if (normalized[0] == L'\\')
		{
			return input;   // UNC, \\?\, \\.\ device paths
		}
		if (!GRedirectRelative || !GGameRootW[0])
		{
			return input;
		}
		StringCchCopyW(output, count, GGameRootW);
		StringCchCatW(output, count, L"\\");
		StringCchCatW(output, count, normalized);
		return output;
	}

	static const char* RedirectA(const char* input, char* output, size_t count)
	{
		if (!input || !input[0] || IsDeviceNameA(input))
		{
			return input;
		}

		char normalized[1024];
		size_t length = 0;
		while (input[length] && length < ARRAYSIZE(normalized) - 1)
		{
			normalized[length] = (input[length] == '/') ? '\\' : input[length];
			++length;
		}
		normalized[length] = '\0';
		if (length < 2)
		{
			return input;
		}

		if (normalized[1] == ':')
		{
			const char* base = nullptr;
			if (normalized[0] == 'G' || normalized[0] == 'g')
			{
				base = GGameRootA;
			}
			else if (normalized[0] == 'T' || normalized[0] == 't')
			{
				base = GLocalAppDataA;
			}
			else if (normalized[0] == 'D' || normalized[0] == 'd')
			{
				base = GDeveloperA;
			}
			if (!base || !base[0])
			{
				return input;
			}
			StringCchCopyA(output, count, base);
			StringCchCatA(output, count, normalized + 2);
			return output;
		}
		if (normalized[0] == '\\')
		{
			return input;
		}
		if (!GRedirectRelative || !GGameRootA[0])
		{
			return input;
		}
		StringCchCopyA(output, count, GGameRootA);
		StringCchCatA(output, count, "\\");
		StringCchCatA(output, count, normalized);
		return output;
	}

	using CreateFileWSignature = HANDLE (WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
	using CreateFileASignature = HANDLE (WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
	using CreateFile2Signature = HANDLE (WINAPI*)(LPCWSTR, DWORD, DWORD, DWORD, LPCREATEFILE2_EXTENDED_PARAMETERS);
	using CreateDirectoryWSignature = BOOL (WINAPI*)(LPCWSTR, LPSECURITY_ATTRIBUTES);
	using RemoveDirectoryWSignature = BOOL (WINAPI*)(LPCWSTR);
	using DeleteFileWSignature = BOOL (WINAPI*)(LPCWSTR);
	using SetFileAttributesWSignature = BOOL (WINAPI*)(LPCWSTR, DWORD);
	using GetFileAttributesExWSignature = BOOL (WINAPI*)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
	using FindFirstFileWSignature = HANDLE (WINAPI*)(LPCWSTR, LPWIN32_FIND_DATAW);
	using FindFirstFileExWSignature = HANDLE (WINAPI*)(LPCWSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD);
	using MoveFileExWSignature = BOOL (WINAPI*)(LPCWSTR, LPCWSTR, DWORD);
	using WideFopenSignature = FILE* (*)(const wchar_t*, const wchar_t*);
	using FopenSignature = FILE* (*)(const char*, const char*);
	using FopenSSignature = errno_t (*)(FILE**, const char*, const char*);

	static CreateFileWSignature GOriginalCreateFileW = nullptr;
	static CreateFileASignature GOriginalCreateFileA = nullptr;
	static CreateFile2Signature GOriginalCreateFile2 = nullptr;
	static CreateDirectoryWSignature GOriginalCreateDirectoryW = nullptr;
	static RemoveDirectoryWSignature GOriginalRemoveDirectoryW = nullptr;
	static DeleteFileWSignature GOriginalDeleteFileW = nullptr;
	static SetFileAttributesWSignature GOriginalSetFileAttributesW = nullptr;
	static GetFileAttributesExWSignature GOriginalGetFileAttributesExW = nullptr;
	static FindFirstFileWSignature GOriginalFindFirstFileW = nullptr;
	static FindFirstFileExWSignature GOriginalFindFirstFileExW = nullptr;
	static MoveFileExWSignature GOriginalMoveFileExW = nullptr;
	static WideFopenSignature GOriginalWideFopen = nullptr;
	static FopenSignature GOriginalFopen = nullptr;
	static FopenSSignature GOriginalFopenS = nullptr;

	static HANDLE WINAPI HookedCreateFileW(LPCWSTR fileName, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
	                                       DWORD creation, DWORD flags, HANDLE templateFile)
	{
		wchar_t buffer[1024];
		return GOriginalCreateFileW(RedirectW(fileName, buffer, ARRAYSIZE(buffer)), access, share, security, creation, flags, templateFile);
	}

	static HANDLE WINAPI HookedCreateFileA(LPCSTR fileName, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
	                                       DWORD creation, DWORD flags, HANDLE templateFile)
	{
		char buffer[1024];
		return GOriginalCreateFileA(RedirectA(fileName, buffer, ARRAYSIZE(buffer)), access, share, security, creation, flags, templateFile);
	}

	static HANDLE WINAPI HookedCreateFile2(LPCWSTR fileName, DWORD access, DWORD share, DWORD creation,
	                                       LPCREATEFILE2_EXTENDED_PARAMETERS parameters)
	{
		wchar_t buffer[1024];
		return GOriginalCreateFile2(RedirectW(fileName, buffer, ARRAYSIZE(buffer)), access, share, creation, parameters);
	}

	static BOOL WINAPI HookedCreateDirectoryW(LPCWSTR pathName, LPSECURITY_ATTRIBUTES security)
	{
		wchar_t buffer[1024];
		return GOriginalCreateDirectoryW(RedirectW(pathName, buffer, ARRAYSIZE(buffer)), security);
	}

	static BOOL WINAPI HookedRemoveDirectoryW(LPCWSTR pathName)
	{
		wchar_t buffer[1024];
		return GOriginalRemoveDirectoryW(RedirectW(pathName, buffer, ARRAYSIZE(buffer)));
	}

	static BOOL WINAPI HookedDeleteFileW(LPCWSTR fileName)
	{
		wchar_t buffer[1024];
		return GOriginalDeleteFileW(RedirectW(fileName, buffer, ARRAYSIZE(buffer)));
	}

	static BOOL WINAPI HookedSetFileAttributesW(LPCWSTR fileName, DWORD attributes)
	{
		wchar_t buffer[1024];
		return GOriginalSetFileAttributesW(RedirectW(fileName, buffer, ARRAYSIZE(buffer)), attributes);
	}

	static BOOL WINAPI HookedGetFileAttributesExW(LPCWSTR fileName, GET_FILEEX_INFO_LEVELS level, LPVOID information)
	{
		wchar_t buffer[1024];
		return GOriginalGetFileAttributesExW(RedirectW(fileName, buffer, ARRAYSIZE(buffer)), level, information);
	}

	static HANDLE WINAPI HookedFindFirstFileW(LPCWSTR fileName, LPWIN32_FIND_DATAW findData)
	{
		wchar_t buffer[1024];
		return GOriginalFindFirstFileW(RedirectW(fileName, buffer, ARRAYSIZE(buffer)), findData);
	}

	static HANDLE WINAPI HookedFindFirstFileExW(LPCWSTR fileName, FINDEX_INFO_LEVELS level, LPVOID findData,
	                                            FINDEX_SEARCH_OPS searchOp, LPVOID filter, DWORD flags)
	{
		wchar_t buffer[1024];
		return GOriginalFindFirstFileExW(RedirectW(fileName, buffer, ARRAYSIZE(buffer)), level, findData, searchOp, filter, flags);
	}

	static BOOL WINAPI HookedMoveFileExW(LPCWSTR existing, LPCWSTR replacement, DWORD flags)
	{
		wchar_t existingBuffer[1024];
		wchar_t replacementBuffer[1024];
		return GOriginalMoveFileExW(RedirectW(existing, existingBuffer, ARRAYSIZE(existingBuffer)),
		                            RedirectW(replacement, replacementBuffer, ARRAYSIZE(replacementBuffer)), flags);
	}

	static FILE* HookedWideFopen(const wchar_t* fileName, const wchar_t* mode)
	{
		wchar_t buffer[1024];
		return GOriginalWideFopen(RedirectW(fileName, buffer, ARRAYSIZE(buffer)), mode);
	}

	static FILE* HookedFopen(const char* fileName, const char* mode)
	{
		char buffer[1024];
		return GOriginalFopen(RedirectA(fileName, buffer, ARRAYSIZE(buffer)), mode);
	}

	static errno_t HookedFopenS(FILE** stream, const char* fileName, const char* mode)
	{
		char buffer[1024];
		return GOriginalFopenS(stream, RedirectA(fileName, buffer, ARRAYSIZE(buffer)), mode);
	}

	struct HookEntry
	{
		const char* name;
		void* hook;
		void** original;
	};

	static const HookEntry GHooks[] =
	{
		{ "CreateFileW",          &HookedCreateFileW,          (void**)&GOriginalCreateFileW },
		{ "CreateFileA",          &HookedCreateFileA,          (void**)&GOriginalCreateFileA },
		{ "CreateFile2",          &HookedCreateFile2,          (void**)&GOriginalCreateFile2 },
		{ "CreateDirectoryW",     &HookedCreateDirectoryW,     (void**)&GOriginalCreateDirectoryW },
		{ "RemoveDirectoryW",     &HookedRemoveDirectoryW,     (void**)&GOriginalRemoveDirectoryW },
		{ "DeleteFileW",          &HookedDeleteFileW,          (void**)&GOriginalDeleteFileW },
		{ "SetFileAttributesW",   &HookedSetFileAttributesW,   (void**)&GOriginalSetFileAttributesW },
		{ "GetFileAttributesExW", &HookedGetFileAttributesExW, (void**)&GOriginalGetFileAttributesExW },
		{ "FindFirstFileW",       &HookedFindFirstFileW,       (void**)&GOriginalFindFirstFileW },
		{ "FindFirstFileExW",     &HookedFindFirstFileExW,     (void**)&GOriginalFindFirstFileExW },
		{ "MoveFileExW",          &HookedMoveFileExW,          (void**)&GOriginalMoveFileExW },
		{ "_wfopen",              &HookedWideFopen,            (void**)&GOriginalWideFopen },
		{ "fopen",                &HookedFopen,                (void**)&GOriginalFopen },
		{ "fopen_s",              &HookedFopenS,               (void**)&GOriginalFopenS },
	};

	static int PatchModule(HMODULE module, bool install)
	{
		if (!module || module == GSelf)
		{
			return 0;   // never patch our own imports
		}
		BYTE* base = (BYTE*)module;
		IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
		if (IsBadReadPtr(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE)
		{
			return 0;
		}
		IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE)
		{
			return 0;
		}
		IMAGE_DATA_DIRECTORY& importDirectory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
		if (!importDirectory.VirtualAddress || !importDirectory.Size)
		{
			return 0;
		}

		int count = 0;
		for (IMAGE_IMPORT_DESCRIPTOR* descriptor = (IMAGE_IMPORT_DESCRIPTOR*)(base + importDirectory.VirtualAddress);
		     descriptor->Name; ++descriptor)
		{
			if (!descriptor->FirstThunk || !descriptor->OriginalFirstThunk)
			{
				continue;
			}
			IMAGE_THUNK_DATA* thunk = (IMAGE_THUNK_DATA*)(base + descriptor->FirstThunk);
			IMAGE_THUNK_DATA* named = (IMAGE_THUNK_DATA*)(base + descriptor->OriginalFirstThunk);
			for (; named->u1.AddressOfData; ++named, ++thunk)
			{
				if (named->u1.Ordinal & IMAGE_ORDINAL_FLAG)
				{
					continue;   // imported by ordinal
				}
				IMAGE_IMPORT_BY_NAME* importByName = (IMAGE_IMPORT_BY_NAME*)(base + named->u1.AddressOfData);
				for (const HookEntry& entry : GHooks)
				{
					if (lstrcmpA((const char*)importByName->Name, entry.name) != 0)
					{
						continue;
					}
					void* target = install ? entry.hook : *entry.original;
					if (!target)
					{
						continue;
					}
					DWORD previousProtection;
					if (!VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &previousProtection))
					{
						continue;
					}
					if (install && !*entry.original)
					{
						*entry.original = (void*)thunk->u1.Function;
					}
					thunk->u1.Function = (ULONGLONG)target;
					VirtualProtect(&thunk->u1.Function, sizeof(void*), previousProtection, &previousProtection);
					++count;
					break;
				}
			}
		}
		return count;
	}

	static void PatchAllModules(bool install)
	{
		HMODULE modules[512];
		DWORD needed = 0;
		if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
		{
			return;
		}
		DWORD count = needed / sizeof(HMODULE);
		if (count > ARRAYSIZE(modules))
		{
			count = ARRAYSIZE(modules);
		}
		for (DWORD i = 0; i < count; ++i)
		{
			PatchModule(modules[i], install);
		}
	}

	static void StripLastComponent(wchar_t* path)
	{
		int length = lstrlenW(path);
		while (length > 0 && path[length - 1] != L'\\')
		{
			--length;
		}
		if (length > 0)
		{
			path[length - 1] = L'\0';
		}
	}

	static void EnsureDirectory(const wchar_t* path)
	{
		wchar_t partial[MAX_PATH];
		StringCchCopyW(partial, MAX_PATH, path);
		for (int i = 0; partial[i]; ++i)
		{
			if (partial[i] == L'\\' && i > 2)
			{
				partial[i] = L'\0';
				CreateDirectoryW(partial, nullptr);
				partial[i] = L'\\';
			}
		}
		CreateDirectoryW(partial, nullptr);
	}

	void InstallPathHooks(HMODULE self)
	{
		GSelf = self;

		char flag[8];
		if (GetEnvironmentVariableA("GDKS_REDIRECT_RELATIVE", flag, sizeof(flag)) > 0)
		{
			GRedirectRelative = (flag[0] == '1');
		}

		// G:\ -> game root (this DLL's dir up 3), unless overridden.
		if (GetEnvironmentVariableW(L"GDKS_G", GGameRootW, MAX_PATH) == 0)
		{
			GetModuleFileNameW(self, GGameRootW, MAX_PATH);
			for (int i = 0; i < 4; ++i)   // filename + 3 dirs
			{
				StripLastComponent(GGameRootW);
			}
		}
		// T:\ -> %LOCALAPPDATA%, unless overridden.
		if (GetEnvironmentVariableW(L"GDKS_T", GLocalAppDataW, MAX_PATH) == 0)
		{
			GetEnvironmentVariableW(L"LOCALAPPDATA", GLocalAppDataW, MAX_PATH);
		}

		// D:\ -> the developer scratch drive, unless overridden. Created up front
		// because the game expects the mount point to already exist.
		if (GetEnvironmentVariableW(L"GDKS_D", GDeveloperW, MAX_PATH) == 0)
		{
			wchar_t localAppData[MAX_PATH];
			if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0)
			{
				StringCchCopyW(GDeveloperW, MAX_PATH, localAppData);
				StringCchCatW(GDeveloperW, MAX_PATH, L"\\GDKScarlett\\Developer");
			}
		}
		if (GDeveloperW[0])
		{
			EnsureDirectory(GDeveloperW);
		}

		WideCharToMultiByte(CP_ACP, 0, GGameRootW, -1, GGameRootA, MAX_PATH, nullptr, nullptr);
		WideCharToMultiByte(CP_ACP, 0, GLocalAppDataW, -1, GLocalAppDataA, MAX_PATH, nullptr, nullptr);
		WideCharToMultiByte(CP_ACP, 0, GDeveloperW, -1, GDeveloperA, MAX_PATH, nullptr, nullptr);

		PatchAllModules(true);
	}

	void RemovePathHooks()
	{
		PatchAllModules(false);
	}
}
