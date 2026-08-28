#pragma once

#include <windows.h>
#include <d3d12.h>

namespace GDKScarlett::D3D12X
{
	void UnregisterCbvHeap(void* realHeap);

	void FlushPlacedUploads(ID3D12CommandQueue* queue);
	void* ReservedResourceForVa(UINT64 va, UINT* startTile, UINT* tileCount);
	void* PagePoolHeap(void* device, UINT64 poolVa, UINT pageCount);

	void NoteBoundCbvHeap(void* realHeap);
	void ShadowCopyForBind(UINT64 gpuHandlePtr, UINT rootParam, bool isCompute);
	bool SrvForGpuHandle(UINT64 gpu, UINT* dim, UINT* fmt, void** res);
	void QueuePlacedUpload(void* res);
}
