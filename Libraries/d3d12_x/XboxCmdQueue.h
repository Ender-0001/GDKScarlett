#pragma once

#include <windows.h>
#include <d3d12.h>

ID3D12CommandQueue* XboxCommandQueueWrap(ID3D12CommandQueue* real, ID3D12Device* device);

namespace GDKScarlett::D3D12X
{
	void* PresentSource();
	bool IsPresentTarget(void* resource);

	// Presented frames. XboxDevice.cpp retires CBV shadows on this clock.
	LONG PresentedFrameCount();
	LONG AdvancePresentedFrame();
}