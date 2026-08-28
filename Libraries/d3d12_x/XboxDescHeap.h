#pragma once
#include <d3d12.h>

ID3D12DescriptorHeap* XboxDescriptorHeapWrap(ID3D12DescriptorHeap* real, ID3D12Device* device);
ID3D12DescriptorHeap* XboxDescriptorHeapUnwrap(ID3D12DescriptorHeap* p);