#include <windows.h>
#include <psapi.h>

#define GDKS_TRACE_TAG "xmem"
#include "Trace.h"

#pragma comment(lib, "psapi.lib")

struct XMEM_WORKING_SET_STATISTICS
{
	unsigned long long GameLimit;                      // 0x00
	unsigned long long GameUsed;                       // 0x08
	unsigned long long ToolsLimit;                     // 0x10
	unsigned long long ToolsUsed;                      // 0x18
	unsigned int       StackUsed;                      // 0x20
	unsigned int       GameAllocatedPhysicalPageCount; // 0x24
	unsigned long long GpuOptimalBandwidthLimit;       // 0x28
	unsigned long long GpuOptimalBandwidthUsed;        // 0x30
	unsigned long long ToolsGpuOptimalBandwidthLimit;  // 0x38
	unsigned long long ToolsGpuOptimalBandwidthUsed;   // 0x40
	unsigned int       KernelPoolUsed;                 // 0x48
	unsigned int       KernelPoolLimit;                // 0x4c
	unsigned int       DirectStorageUsed;              // 0x50
	unsigned int       FramePreserveBufferSize;        // 0x54
	unsigned long long GameUsedPeak;                   // 0x58
	unsigned int       GpuPageTableSize;               // 0x60
	unsigned int       CpuPageTableSize;               // 0x64
	unsigned long long reserved1[2];                   // 0x68
};
static_assert(sizeof(XMEM_WORKING_SET_STATISTICS) == 0x78);

EXTERN_C void* XMemAlloc(SIZE_T size, unsigned long long)
{
	return VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

EXTERN_C void* XMemAllocDefault(SIZE_T size, unsigned long long)
{
	return VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

EXTERN_C void XMemFree(void* address, unsigned long long)
{
	if (address)
	{
		VirtualFree(address, 0, MEM_RELEASE);
	}
}

EXTERN_C void XMemFreeDefault(void* address, unsigned long long)
{
	if (address)
	{
		VirtualFree(address, 0, MEM_RELEASE);
	}
}

EXTERN_C void* XMemVirtualAlloc(void* baseAddress, SIZE_T size, DWORD allocationType,
	ULONGLONG, DWORD pageProtection)
{
	// Strip Xbox-only allocation flags desktop VirtualAlloc rejects
	const DWORD desktopMask = MEM_COMMIT | MEM_RESERVE | MEM_TOP_DOWN |
	                          MEM_RESET | MEM_RESET_UNDO | MEM_WRITE_WATCH;
	DWORD type = allocationType & desktopMask;
	if (!type)
	{
		type = MEM_COMMIT | MEM_RESERVE;
	}
	// TODO: translate pageProtection from the Xbox PAGE_GRAPHICS_?
	(void)pageProtection;
	return VirtualAlloc(baseAddress, size, type, PAGE_READWRITE);
}

EXTERN_C HRESULT XMemTransferMemory(void*, void*, SIZE_T, unsigned long long)
{
	return S_OK;
}

EXTERN_C HRESULT XMemGetAllocationStatistics(void*)
{
	return S_OK;
}

EXTERN_C BOOL WINAPI XMemGetWorkingSetStatistics(DWORD, XMEM_WORKING_SET_STATISTICS* statistics)
{
	if (!statistics)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	ZeroMemory(statistics, sizeof(*statistics));

	// Plausible title budget plus the process's real commit
	statistics->GameLimit = 8ULL * 1024 * 1024 * 1024;

	PROCESS_MEMORY_COUNTERS_EX counters{};
	counters.cb = sizeof(counters);
	if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&counters, sizeof(counters)))
	{
		statistics->GameUsed = counters.PrivateUsage;
		statistics->GameUsedPeak = counters.PeakWorkingSetSize;
		statistics->GameAllocatedPhysicalPageCount = (unsigned)(counters.WorkingSetSize / 4096);
	}
	return TRUE;
}

EXTERN_C BOOL XMemIsToolsMemAvailable()
{
	return FALSE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hModule);
		LOGF("DLL_PROCESS_ATTACH");
	}
	return TRUE;
}
