#pragma once
#include <d3d12.h>

ID3D12GraphicsCommandList* XboxCommandListWrap(ID3D12GraphicsCommandList* real, ID3D12Device* device);
ID3D12GraphicsCommandList* XboxCommandListUnwrap(ID3D12GraphicsCommandList* p);