#define GDKS_TRACE_TAG "d3d12_xs"
#include "Common.h"
#include <d3d12.h>

struct D3D12XBOX_CREATE_DEVICE_PARAMETERS
{
	UINT      Version;                                   // 0x00
	UINT      ProcessDebugFlags;                         // 0x04
	UINT      GraphicsCommandQueueRingSizeBytes;         // 0x08
	UINT64    pOffchipTessellationBuffer;                // 0x10
	UINT      GraphicsScratchMemorySizeBytes;            // 0x18
	UINT      ComputeScratchMemorySizeBytes;             // 0x1c
	BOOL      DisableGeometryShaderAllocations;          // 0x20
	BOOL      DisableTessellationShaderAllocations;      // 0x24
	BOOL      DisableDXR;                                // 0x28
	BOOL      DisableAutomaticDPBBBreakBatchEvents;      // 0x2c
	UINT64    pDXRStackBuffer;                           // 0x30
	UINT      DXRStackBufferOverrideSizeBytes;           // 0x38
	UINT      CreateDeviceFlags;                         // 0x3c
	UINT      AutoHDRPaperWhiteLevelNits;                // 0x40
	BOOL      DisableAutomaticCommandSegmentChaining;    // 0x44
	UINT      AmplificationShaderIndirectArgsBufferSize; // 0x48
	UINT      AmplificationShaderPayloadBufferSize;      // 0x4c
};
static_assert(sizeof(D3D12XBOX_CREATE_DEVICE_PARAMETERS) == 0x50);

EXTERN_C const GUID IID_ID3D12Device =
	{ 0x189819f1, 0x1db6, 0x4b57, { 0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7 } };

EXTERN_C HRESULT D3D12XboxCreateDevice(IUnknown*, const D3D12XBOX_CREATE_DEVICE_PARAMETERS* params,
                                       REFIID, void** ppDevice)
{
	if (params)
	{
		LOGF("D3D12XboxCreateDevice: Version=%u DebugFlags=0x%X CreateFlags=0x%X DisableDXR=%d (stub)",
		     params->Version, params->ProcessDebugFlags, params->CreateDeviceFlags, params->DisableDXR);
	}
	else
	{
		TRACE();
	}
	if (ppDevice)
	{
		*ppDevice = nullptr;
	}
	return E_NOTIMPL;
}

EXTERN_C HRESULT D3D12CreateDevice(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void** ppDevice)
{
	TRACE();
	if (ppDevice)
	{
		*ppDevice = nullptr;
	}
	return E_NOTIMPL;
}

EXTERN_C HRESULT D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION,
                                             ID3DBlob** ppBlob, ID3DBlob** ppError)
{
	TRACE();
	if (ppBlob)
	{
		*ppBlob = nullptr;
	}
	if (ppError)
	{
		*ppError = nullptr;
	}
	return E_NOTIMPL;
}

EXTERN_C HRESULT D3D12SerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*,
                                                      ID3DBlob** ppBlob, ID3DBlob** ppError)
{
	TRACE();
	if (ppBlob)
	{
		*ppBlob = nullptr;
	}
	if (ppError)
	{
		*ppError = nullptr;
	}
	return E_NOTIMPL;
}

EXTERN_C HRESULT D3D12CreateVersionedRootSignatureDeserializer(LPCVOID, SIZE_T, REFIID, void** ppv)
{
	TRACE();
	if (ppv)
	{
		*ppv = nullptr;
	}
	return E_NOTIMPL;
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