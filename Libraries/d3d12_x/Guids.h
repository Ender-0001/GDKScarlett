#pragma once
#include <windows.h>

// Defined across the wrapper translation units rather than linked from
// dxguid.lib; see XboxDescHeap.cpp.
EXTERN_C const GUID IID_ID3D12Device;
EXTERN_C const GUID IID_ID3D12Object;
EXTERN_C const GUID IID_ID3D12DeviceChild;
EXTERN_C const GUID IID_ID3D12Pageable;
EXTERN_C const GUID IID_ID3D12CommandQueue;
EXTERN_C const GUID IID_ID3D12Resource;
EXTERN_C const GUID IID_ID3D12Fence;
EXTERN_C const GUID IID_ID3D12CommandAllocator;
