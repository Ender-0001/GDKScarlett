#pragma once

#include <windows.h>
#include <d3d12.h>
#include <unknwn.h>

IUnknown* XboxDxgiDeviceCreate(ID3D12Device* realDevice);