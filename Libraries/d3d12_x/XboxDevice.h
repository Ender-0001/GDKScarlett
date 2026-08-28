#pragma once

#include <windows.h>
#include <d3d12.h>

ID3D12Device* XboxDeviceCreate(ID3D12Device* real);

namespace GDKScarlett::D3D12X
{
	void DrainInfoQueue(ID3D12Device* real, const char* who, UINT64 from = 0);
	UINT64 MarkInfoQueue(ID3D12Device* real);
	void ReportDeviceRemoved(ID3D12Device* real, const char* who);
}
