#define GDKS_TRACE_TAG "d3d12_x"
#include "Common.h"
#include <d3d12.h>
#include "Guids.h"
#include "XboxDevice.h"

struct D3D12XBOX_CREATE_DEVICE_PARAMETERS
{
	UINT      Version;                              // 0x00
	UINT      ProcessDebugFlags;                    // 0x04
	UINT64    GraphicsCommandQueueRingSizeBytes;    // 0x08
	void*     pOffchipTessellationBuffer;           // 0x10
	UINT      GraphicsScratchMemorySizeBytes;       // 0x18
	UINT      ComputeScratchMemorySizeBytes;        // 0x1c
	UINT      DisableGeometryShaderAllocations;     // 0x20
	UINT      DisableTessellationShaderAllocations; // 0x24
};
static_assert(sizeof(D3D12XBOX_CREATE_DEVICE_PARAMETERS) == 0x28);

static HMODULE RealD3D12()
{
	static HMODULE module = LoadLibraryW(L"d3d12.dll");
	return module;
}

static FARPROC RealProc(const char* name)
{
	HMODULE module = RealD3D12();
	FARPROC proc = module ? GetProcAddress(module, name) : nullptr;
	if (!proc)
	{
		LOGF("could not resolve d3d12.dll!%s", name);
	}
	return proc;
}

EXTERN_C const GUID IID_ID3D12Device =
	{ 0x189819f1, 0x1db6, 0x4b57, { 0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7 } };

EXTERN_C HRESULT D3D12XboxCreateDevice(IUnknown* adapter, const D3D12XBOX_CREATE_DEVICE_PARAMETERS* params,
                                       REFIID, void** ppDevice)
{
	if (params)
	{
		LOGF("D3D12XboxCreateDevice: Version=%u DebugFlags=0x%X GfxScratch=%u CmpScratch=%u",
		     params->Version, params->ProcessDebugFlags,
		     params->GraphicsScratchMemorySizeBytes, params->ComputeScratchMemorySizeBytes);
	}
	if (!ppDevice)
	{
		return E_POINTER;
	}
	*ppDevice = nullptr;

	auto create = (HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**))RealProc("D3D12CreateDevice");
	if (!create)
	{
		return E_FAIL;
	}

	static const D3D_FEATURE_LEVEL levels[] =
	{
#ifdef D3D_FEATURE_LEVEL_12_2
		D3D_FEATURE_LEVEL_12_2,
#endif
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
	};
	HRESULT hr = E_FAIL;
	ID3D12Device* realDevice = nullptr;
	for (D3D_FEATURE_LEVEL level : levels)
	{
		hr = create(adapter, level, IID_ID3D12Device, (void**)&realDevice);
		if (SUCCEEDED(hr))
		{
			*ppDevice = XboxDeviceCreate(realDevice);
			LOGF("real device at feature level 0x%X -> %p, wrapped -> %p", (unsigned)level, realDevice, *ppDevice);
			return hr;
		}
	}
	LOGF("D3D12CreateDevice failed, hr=0x%08X", (unsigned)hr);
	return hr;
}

EXTERN_C HRESULT D3D12CreateDevice(IUnknown* adapter, D3D_FEATURE_LEVEL featureLevel, REFIID riid, void** ppDevice)
{
	auto fn = (HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**))RealProc("D3D12CreateDevice");
	return fn ? fn(adapter, featureLevel, riid, ppDevice) : E_FAIL;
}

EXTERN_C HRESULT D3D12GetDebugInterface(REFIID riid, void** ppv)
{
	auto fn = (HRESULT(WINAPI*)(REFIID, void**))RealProc("D3D12GetDebugInterface");
	return fn ? fn(riid, ppv) : E_NOTIMPL;
}

EXTERN_C HRESULT D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC* desc, D3D_ROOT_SIGNATURE_VERSION version,
                                             ID3DBlob** ppBlob, ID3DBlob** ppError)
{
	auto fn = (HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**))
	          RealProc("D3D12SerializeRootSignature");
	return fn ? fn(desc, version, ppBlob, ppError) : E_FAIL;
}

EXTERN_C HRESULT D3D12SerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc,
                                                      ID3DBlob** ppBlob, ID3DBlob** ppError)
{
	auto fn = (HRESULT(WINAPI*)(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*, ID3DBlob**, ID3DBlob**))
	          RealProc("D3D12SerializeVersionedRootSignature");
	return fn ? fn(desc, ppBlob, ppError) : E_FAIL;
}

EXTERN_C HRESULT D3D12CreateVersionedRootSignatureDeserializer(LPCVOID data, SIZE_T size, REFIID riid, void** ppv)
{
	auto fn = (HRESULT(WINAPI*)(LPCVOID, SIZE_T, REFIID, void**))
	          RealProc("D3D12CreateVersionedRootSignatureDeserializer");
	return fn ? fn(data, size, riid, ppv) : E_FAIL;
}

// Xbox One ESRAM page mapping. Desktop has no ESRAM, so back the VA range with plain committed RAM (unified-memory approach).
EXTERN_C HRESULT D3DMapEsramMemory(unsigned flags, void* virtualAddress, unsigned pageCount, unsigned* pageList)
{
	SIZE_T bytes = (SIZE_T)pageCount * 65536;   // ESRAM page = 64KB
	LOGF("D3DMapEsramMemory: flags=0x%X va=%p pages=%u (%zu KB) pageList=%p",
	     flags, virtualAddress, pageCount, bytes / 1024, pageList);
	if (!virtualAddress || !pageCount)
	{
		return S_OK;
	}
	BYTE* cursor = (BYTE*)virtualAddress;
	BYTE* end = cursor + bytes;
	while (cursor < end)
	{
		MEMORY_BASIC_INFORMATION memInfo{};
		if (!VirtualQuery(cursor, &memInfo, sizeof(memInfo)))
		{
			break;
		}
		BYTE* regionEnd = (BYTE*)memInfo.BaseAddress + memInfo.RegionSize;
		SIZE_T span = (SIZE_T)((regionEnd < end ? regionEnd : end) - cursor);
		if (memInfo.State == MEM_RESERVE)
		{
			if (!VirtualAlloc(cursor, span, MEM_COMMIT, PAGE_READWRITE))
			{
				LOGF("D3DMapEsramMemory: commit at %p (%zu KB) FAILED (gle=%lu)", cursor, span / 1024, GetLastError());
			}
		}
		else if (memInfo.State == MEM_FREE)
		{
			if (!VirtualAlloc(cursor, span, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
			{
				LOGF("D3DMapEsramMemory: reserve+commit at %p (%zu KB) FAILED (gle=%lu)", cursor, span / 1024, GetLastError());
			}
		}
		cursor += span;
	}
	return S_OK;
}

EXTERN_C HRESULT D3D12XboxSetProcessDebugFlags(unsigned flags)
{
	LOGF("D3D12XboxSetProcessDebugFlags(0x%X)", flags);
	return S_OK;
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
