#define GDKS_TRACE_TAG "d3d12_x"
#include "Common.h"

#include "XboxCmdQueue.h"

#include "Guids.h"
#include "XboxCmdList.h"
#include "XboxDevice.h"
#include "XboxDeviceInternal.h"

#include <dxgi1_4.h>
#pragma comment(lib, "dxgi.lib")

static volatile LONG GPresentedFrames = 0;

LONG GDKScarlett::D3D12X::PresentedFrameCount()
{
	return GPresentedFrames;
}

LONG GDKScarlett::D3D12X::AdvancePresentedFrame()
{
	return InterlockedIncrement(&GPresentedFrames);
}
static ID3D12Resource* GPresentSource = nullptr;
static ID3D12Resource* GPresentSet[4] = {};
static LONG GPresentSetCount = 0;

void* GDKScarlett::D3D12X::PresentSource()
{
	return GPresentSource;
}

bool GDKScarlett::D3D12X::IsPresentTarget(void* resource)
{
	if (!resource)
	{
		return 0;
	}
	LONG count = GPresentSetCount;
	for (LONG i = 0; i < count && i < 4; ++i)
	{
		if (GPresentSet[i] == resource)
		{
			return 1;
		}
	}
	return 0;
}

static void NotePresentTarget(ID3D12Resource* resource)
{
	if (!resource || GDKScarlett::D3D12X::IsPresentTarget(resource))
	{
		return;
	}
	LONG index = GPresentSetCount;
	if (index < 4)
	{
		GPresentSet[index] = resource;
		GPresentSetCount = index + 1;
	}
}

EXTERN_C const GUID IID_ID3D12CommandQueue =
	{ 0x0ec870a6, 0x5d7e, 0x4c22, { 0x8c, 0xfc, 0x5b, 0xaa, 0xe0, 0x76, 0x16, 0xed } };

enum D3D12XBOX_PAGE_MAPPING_RANGE_TYPE
{
	XBOX_RANGE_INCREMENTING_PAGE_INDICES = 0,
	XBOX_RANGE_CONSTANT_PAGE_INDEX = 1,
	XBOX_RANGE_NULL_PAGES = 2,
	XBOX_RANGE_SKIP_RANGE = 3,
};

struct D3D12XBOX_PAGE_MAPPING_RANGE
{
	D3D12XBOX_PAGE_MAPPING_RANGE_TYPE RangeType;
	UINT StartPageIndexInPool;
	UINT PageCount;
};

struct D3D12XBOX_PAGE_MAPPING_BATCH
{
	D3D12_GPU_VIRTUAL_ADDRESS DestinationAddress;
	UINT RangeCount;
	const D3D12XBOX_PAGE_MAPPING_RANGE* pRanges;
};

struct D3D12XBOX_PRESENT_PLANE_PARAMETERS
{
	UINT64 Token;
	UINT ResourceCount;
	ID3D12Resource** ppResources;
	D3D12_RECT* pSrcViewRects;
	void* pDestPlacementBase;
	UINT ColorSpace;
	UINT ScaleFilter;
	UINT ExtendedDescCount;
	void* pExtendedDescs;
	UINT Flags;
};

struct IXboxCommandQueue
{
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) = 0;
	virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
	virtual ULONG STDMETHODCALLTYPE Release() = 0;
	virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* size, void* data) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT size, const void* data) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* data) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetName(LPCWSTR name) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppv) = 0;
	virtual void STDMETHODCALLTYPE UpdateTileMappings(
		ID3D12Resource* resource, UINT numResourceRegions,
		const D3D12_TILED_RESOURCE_COORDINATE* regionStartCoordinates,
		const D3D12_TILE_REGION_SIZE* regionSizes, ID3D12Heap* heap, UINT numRanges,
		const D3D12_TILE_RANGE_FLAGS* rangeFlags, const UINT* heapRangeStartOffsets,
		const UINT* rangeTileCounts, D3D12_TILE_MAPPING_FLAGS flags) = 0;
	virtual void STDMETHODCALLTYPE CopyTileMappings(
		ID3D12Resource* dstResource, const D3D12_TILED_RESOURCE_COORDINATE* dstRegionStartCoordinate,
		ID3D12Resource* srcResource, const D3D12_TILED_RESOURCE_COORDINATE* srcRegionStartCoordinate,
		const D3D12_TILE_REGION_SIZE* regionSize, D3D12_TILE_MAPPING_FLAGS flags) = 0;
	virtual void STDMETHODCALLTYPE ExecuteCommandLists(UINT numCommandLists,
	                                                  ID3D12CommandList* const* commandLists) = 0;
	virtual void STDMETHODCALLTYPE SetMarker(UINT metadata, const void* data, UINT size) = 0;
	virtual void STDMETHODCALLTYPE BeginEvent(UINT metadata, const void* data, UINT size) = 0;
	virtual void STDMETHODCALLTYPE EndEvent() = 0;
	virtual HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence* fence, UINT64 value) = 0;
	virtual HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence* fence, UINT64 value) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetTimestampFrequency(UINT64* frequency) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetClockCalibration(UINT64* gpuTimestamp, UINT64* cpuTimestamp) = 0;
	virtual HRESULT STDMETHODCALLTYPE PIXGpuCaptureNextFrame(void*, void*, void*, void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE PIXGpuBeginCapture(void*, void*, void*, void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE PIXGpuEndCapture(void*, void*, void*, void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE SuspendX(void*, void*, void*, void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE ResumeX(void*, void*, void*, void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE SignalX(void* fence, void* value, void*, void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE InvalidateGraphicsTLBX(void*, void*, void*, void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE KickoffX(void*, void*, void*, void*) = 0;
	virtual D3D12_COMMAND_QUEUE_DESC* STDMETHODCALLTYPE GetDesc(IXboxCommandQueue* queue) = 0;
	virtual HRESULT STDMETHODCALLTYPE CopyPageMappingsX(
		D3D12_GPU_VIRTUAL_ADDRESS destinationAddress, UINT rangeCount,
		const D3D12XBOX_PAGE_MAPPING_RANGE* ranges,
		D3D12_GPU_VIRTUAL_ADDRESS sourcePagePoolAddress, UINT sourcePagePoolPageCount, UINT flags) = 0;
	virtual HRESULT STDMETHODCALLTYPE ClearPageMappingsX(void*, void*, void*, void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE CopyPageMappingsBatchX(
		UINT batchCount, const D3D12XBOX_PAGE_MAPPING_BATCH* batches,
		D3D12_GPU_VIRTUAL_ADDRESS sourcePagePoolAddress, UINT sourcePagePoolPageCount, UINT flags) = 0;
	virtual HRESULT STDMETHODCALLTYPE PresentX(UINT planeCount,
	                                           const D3D12XBOX_PRESENT_PLANE_PARAMETERS* planes,
	                                           const void* presentParameters) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetHDRToneMapperX(void*, void*, void*, void*) = 0;
	virtual void STDMETHODCALLTYPE ExecuteCommandLists2X(UINT count,
	                                                     ID3D12CommandList* const* commandLists,
	                                                     UINT64 token, UINT flags) = 0;
	virtual HRESULT STDMETHODCALLTYPE Signal2X(void* fence, void* value, void*, void*) = 0;
};
static_assert(sizeof(IXboxCommandQueue) == sizeof(void*),
              "IXboxCommandQueue must stay a pure vtable: no data members, single inheritance");

struct Presenter
{
	static const UINT kRing = 3;

	IDXGISwapChain3* swap = nullptr;
	ID3D12GraphicsCommandList* list = nullptr;
	ID3D12Device* device = nullptr;
	HWND hwnd = nullptr;
	UINT width = 0;
	UINT height = 0;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

	// Resetting one allocator per present while its previous submission is still
	// in flight corrupts the commands it recorded, so a slot is only reset once
	// the GPU has passed the fence value recorded when it was last submitted.
	ID3D12CommandAllocator* ring[kRing] = {};
	UINT64 ringFence[kRing] = {};
	UINT current = 0;
	ID3D12CommandAllocator* allocator = nullptr;
	ID3D12Fence* fence = nullptr;
	UINT64 fenceValue = 0;
	HANDLE fenceEvent = nullptr;
};

static Presenter GPresenter;
static SRWLOCK GPresentLock = SRWLOCK_INIT;

// The game creates its own window, so adopt it rather than making a second one.
// xmem's debug console is also a top-level visible window owned by this process,
// so skip it explicitly and take the largest remaining client area.
static BOOL CALLBACK FindGameWindow(HWND window, LPARAM parameter)
{
	DWORD pid = 0;
	GetWindowThreadProcessId(window, &pid);
	if (pid != GetCurrentProcessId())
	{
		return TRUE;
	}
	if (window == GetConsoleWindow())
	{
		return TRUE;
	}
	if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER))
	{
		return TRUE;
	}
	if (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
	{
		return TRUE;
	}

	char className[64] = {};
	GetClassNameA(window, className, sizeof(className));
	if (lstrcmpiA(className, "ConsoleWindowClass") == 0 ||
	    lstrcmpiA(className, "CASCADIA_HOSTING_WINDOW_CLASS") == 0)
	{
		return TRUE;
	}

	RECT client = {};
	GetClientRect(window, &client);
	LONG width = client.right - client.left;
	LONG height = client.bottom - client.top;
	if (width < 64 || height < 64)
	{
		return TRUE;
	}

	HWND* best = (HWND*)parameter;
	if (*best)
	{
		RECT bestClient = {};
		GetClientRect(*best, &bestClient);
		if ((bestClient.right - bestClient.left) * (LONGLONG)(bestClient.bottom - bestClient.top) >=
		    width * (LONGLONG)height)
		{
			return TRUE;
		}
	}
	*best = window;
	return TRUE;
}

class XboxCommandQueue final : public IXboxCommandQueue
{
public:
	XboxCommandQueue(ID3D12CommandQueue* real, ID3D12Device* device)
		: mReal(real), mDevice(device)
	{
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
	{
		if (!ppv)
		{
			return E_POINTER;
		}
		if (riid == IID_IUnknown || riid == IID_ID3D12CommandQueue ||
		    riid == IID_ID3D12Pageable || riid == IID_ID3D12DeviceChild || riid == IID_ID3D12Object)
		{
			InterlockedIncrement(&mRefs);
			*ppv = this;
			return S_OK;
		}
		return mReal->QueryInterface(riid, ppv);
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return InterlockedIncrement(&mRefs);
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		LONG refs = InterlockedDecrement(&mRefs);
		if (refs == 0)
		{
			mReal->Release();
			delete this;
		}
		return (ULONG)refs;
	}

	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* size, void* data) override
	{
		return mReal->GetPrivateData(guid, size, data);
	}

	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT size, const void* data) override
	{
		return mReal->SetPrivateData(guid, size, data);
	}

	HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* data) override
	{
		return mReal->SetPrivateDataInterface(guid, data);
	}

	HRESULT STDMETHODCALLTYPE SetName(LPCWSTR name) override
	{
		return mReal->SetName(name);
	}

	// Hand back the device we were created from, so the game keeps seeing the
	// Xbox-shaped device rather than the raw desktop one.
	HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppv) override
	{
		if (mDevice && ppv)
		{
			return mDevice->QueryInterface(riid, ppv);
		}
		return mReal->GetDevice(riid, ppv);
	}

	void STDMETHODCALLTYPE UpdateTileMappings(
		ID3D12Resource* resource, UINT numResourceRegions,
		const D3D12_TILED_RESOURCE_COORDINATE* regionStartCoordinates,
		const D3D12_TILE_REGION_SIZE* regionSizes, ID3D12Heap* heap, UINT numRanges,
		const D3D12_TILE_RANGE_FLAGS* rangeFlags, const UINT* heapRangeStartOffsets,
		const UINT* rangeTileCounts, D3D12_TILE_MAPPING_FLAGS flags) override
	{
		mReal->UpdateTileMappings(resource, numResourceRegions, regionStartCoordinates, regionSizes,
		                          heap, numRanges, rangeFlags, heapRangeStartOffsets,
		                          rangeTileCounts, flags);
	}

	void STDMETHODCALLTYPE CopyTileMappings(
		ID3D12Resource* dstResource, const D3D12_TILED_RESOURCE_COORDINATE* dstRegionStartCoordinate,
		ID3D12Resource* srcResource, const D3D12_TILED_RESOURCE_COORDINATE* srcRegionStartCoordinate,
		const D3D12_TILE_REGION_SIZE* regionSize, D3D12_TILE_MAPPING_FLAGS flags) override
	{
		mReal->CopyTileMappings(dstResource, dstRegionStartCoordinate, srcResource,
		                        srcRegionStartCoordinate, regionSize, flags);
	}

	void STDMETHODCALLTYPE ExecuteCommandLists(UINT numCommandLists,
	                                           ID3D12CommandList* const* commandLists) override
	{
		SubmitLists(numCommandLists, commandLists);
	}

	void STDMETHODCALLTYPE SetMarker(UINT metadata, const void* data, UINT size) override
	{
		mReal->SetMarker(metadata, data, size);
	}

	void STDMETHODCALLTYPE BeginEvent(UINT metadata, const void* data, UINT size) override
	{
		mReal->BeginEvent(metadata, data, size);
	}

	void STDMETHODCALLTYPE EndEvent() override
	{
		mReal->EndEvent();
	}

	HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence* fence, UINT64 value) override
	{
		return mReal->Signal(fence, value);
	}

	HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence* fence, UINT64 value) override
	{
		return mReal->Wait(fence, value);
	}

	HRESULT STDMETHODCALLTYPE GetTimestampFrequency(UINT64* frequency) override
	{
		return mReal->GetTimestampFrequency(frequency);
	}

	HRESULT STDMETHODCALLTYPE GetClockCalibration(UINT64* gpuTimestamp, UINT64* cpuTimestamp) override
	{
		return mReal->GetClockCalibration(gpuTimestamp, cpuTimestamp);
	}

	HRESULT STDMETHODCALLTYPE PIXGpuCaptureNextFrame(void*, void*, void*, void*) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE PIXGpuBeginCapture(void*, void*, void*, void*) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE PIXGpuEndCapture(void*, void*, void*, void*) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE SuspendX(void*, void*, void*, void*) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE ResumeX(void*, void*, void*, void*) override
	{
		return S_OK;
	}

	// Signature is unverified, but plausibly (fence, value) like desktop Signal.
	HRESULT STDMETHODCALLTYPE SignalX(void* fence, void* value, void*, void*) override
	{
		if (fence)
		{
			return mReal->Signal((ID3D12Fence*)fence, (UINT64)(UINT_PTR)value);
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE InvalidateGraphicsTLBX(void*, void*, void*, void*) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE KickoffX(void*, void*, void*, void*) override
	{
		return S_OK;
	}

	// Xbox passes the return buffer in the this slot and the object second; MSVC does the reverse.
	D3D12_COMMAND_QUEUE_DESC* STDMETHODCALLTYPE GetDesc(IXboxCommandQueue* queue) override
	{
		static volatile LONG probed = 0;
		if (InterlockedExchange(&probed, 1) == 0)
			LOGF("queue GetDesc: this=%p param=%p", (void*)this, (void*)queue);
		D3D12_COMMAND_QUEUE_DESC* out = (D3D12_COMMAND_QUEUE_DESC*)this;
		*out = static_cast<XboxCommandQueue*>(queue)->mReal->GetDesc();
		return out;
	}

	HRESULT STDMETHODCALLTYPE CopyPageMappingsX(
		D3D12_GPU_VIRTUAL_ADDRESS destinationAddress, UINT rangeCount,
		const D3D12XBOX_PAGE_MAPPING_RANGE* ranges,
		D3D12_GPU_VIRTUAL_ADDRESS sourcePagePoolAddress, UINT sourcePagePoolPageCount,
		UINT flags) override
	{
		(void)flags;
		return ApplyPageMappings(destinationAddress, rangeCount, ranges,
		                         sourcePagePoolAddress, sourcePagePoolPageCount);
	}

	HRESULT STDMETHODCALLTYPE ClearPageMappingsX(void*, void*, void*, void*) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE CopyPageMappingsBatchX(
		UINT batchCount, const D3D12XBOX_PAGE_MAPPING_BATCH* batches,
		D3D12_GPU_VIRTUAL_ADDRESS sourcePagePoolAddress, UINT sourcePagePoolPageCount,
		UINT flags) override
	{
		(void)flags;
		if (!batches)
		{
			return S_OK;
		}
		for (UINT i = 0; i < batchCount; ++i)
		{
			ApplyPageMappings(batches[i].DestinationAddress, batches[i].RangeCount,
			                  batches[i].pRanges, sourcePagePoolAddress, sourcePagePoolPageCount);
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE PresentX(UINT planeCount,
	                                   const D3D12XBOX_PRESENT_PLANE_PARAMETERS* planes,
	                                   const void*) override
	{
		ID3D12Resource* source = nullptr;
		if (planeCount && planes && planes[0].ResourceCount && planes[0].ppResources)
		{
			source = planes[0].ppResources[0];
		}
		if (source)
		{
			GPresentSource = source;
			NotePresentTarget(source);
		}
		// Only plane 0 / resource 0 is presented; anything on further planes is
		// silently dropped, so make that visible.
		{
			UINT resourceCount = (planeCount && planes) ? planes[0].ResourceCount : 0;
			static LONG loggedMultiPlane = 0;
			if ((planeCount > 1 || resourceCount > 1) && InterlockedIncrement(&loggedMultiPlane) <= 8)
			{
				LOGF("PresentX: PlaneCount=%u plane0.ResourceCount=%u - beyond [0][0] is DROPPED",
				     planeCount, resourceCount);
			}
		}

		AcquireSRWLockExclusive(&GPresentLock);
		if (EnsureSwapChain(source))
		{
			PresentFrame(source);
		}
		ReleaseSRWLockExclusive(&GPresentLock);
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE SetHDRToneMapperX(void*, void*, void*, void*) override
	{
		return S_OK;
	}

	// The token and flags only pace the Xbox frame pipeline; the desktop
	// submission is the plain ExecuteCommandLists. This was a no-op stub once,
	// which silently dropped whole frame submissions and parked every thread on
	// waits for GPU work that never ran.
	void STDMETHODCALLTYPE ExecuteCommandLists2X(UINT count, ID3D12CommandList* const* commandLists,
	                                             UINT64 token, UINT flags) override
	{
		static LONG calls = 0;
		LONG current = InterlockedIncrement(&calls);
		if (current <= 4 || (current % 2000) == 0)
		{
			LOGF("ExecuteCommandLists2X: n=%u token=%llu flags=0x%X (forwarding, %ld total)",
			     count, (unsigned long long)token, flags, current);
		}
		SubmitLists(count, commandLists);
	}

	HRESULT STDMETHODCALLTYPE Signal2X(void* fence, void* value, void*, void*) override
	{
		if (fence)
		{
			return mReal->Signal((ID3D12Fence*)fence, (UINT64)(UINT_PTR)value);
		}
		return S_OK;
	}

	ID3D12CommandQueue* Real() const
	{
		return mReal;
	}

private:
	void SubmitLists(UINT numCommandLists, ID3D12CommandList* const* commandLists)
	{
		if (!commandLists || numCommandLists == 0)
		{
			mReal->ExecuteCommandLists(numCommandLists, commandLists);
			return;
		}
		// The game submits the wrapper objects we handed it from
		// CreateCommandList/CreateCommandListX, so unwrap before executing.
		ID3D12CommandList* stackLists[16];
		ID3D12CommandList** lists = stackLists;
		if (numCommandLists > ARRAYSIZE(stackLists))
		{
			lists = (ID3D12CommandList**)HeapAlloc(GetProcessHeap(), 0,
			                                       sizeof(ID3D12CommandList*) * numCommandLists);
			if (!lists)
			{
				return;
			}
		}
		for (UINT i = 0; i < numCommandLists; ++i)
		{
			lists[i] = (ID3D12CommandList*)XboxCommandListUnwrap((ID3D12GraphicsCommandList*)commandLists[i]);
		}
		// Upload any queued game-VA texel data on this queue first, so it lands
		// before the game's lists sample those textures.
		GDKScarlett::D3D12X::FlushPlacedUploads(mReal);
		mReal->ExecuteCommandLists(numCommandLists, lists);
		GDKScarlett::D3D12X::DrainInfoQueue(mDevice, "ExecuteCommandLists");

		// Poll periodically rather than only on a failed call: a wedged queue can
		// go removed without any single call here returning a failure HRESULT.
		static LONG calls = 0;
		if ((InterlockedIncrement(&calls) % 60) == 0)
		{
			HRESULT reason = mDevice->GetDeviceRemovedReason();
			if (reason != S_OK)
			{
				LOGF("*** DEVICE REMOVED (periodic poll, call #%ld) reason=0x%08X",
				     calls, (unsigned)reason);
				static LONG reported = 0;
				if (InterlockedIncrement(&reported) == 1)
				{
					GDKScarlett::D3D12X::ReportDeviceRemoved(mDevice, "periodic poll");
				}
			}
		}
		if (lists != stackLists)
		{
			HeapFree(GetProcessHeap(), 0, lists);
		}
	}

	// Xbox pages and D3D12 tiles are both 64 KB, so page index == tile index.
	HRESULT ApplyPageMappings(D3D12_GPU_VIRTUAL_ADDRESS destinationAddress, UINT rangeCount,
	                          const D3D12XBOX_PAGE_MAPPING_RANGE* ranges,
	                          D3D12_GPU_VIRTUAL_ADDRESS poolAddress, UINT poolPages)
	{
		if (!ranges || !rangeCount)
		{
			return S_OK;
		}
		UINT startTile = 0;
		UINT tileCount = 0;
		ID3D12Resource* resource =
			(ID3D12Resource*)GDKScarlett::D3D12X::ReservedResourceForVa((UINT64)destinationAddress, &startTile, &tileCount);
		if (!resource)
		{
			return S_OK;
		}
		ID3D12Heap* heap = (ID3D12Heap*)GDKScarlett::D3D12X::PagePoolHeap(mDevice, (UINT64)poolAddress, poolPages);
		if (!heap && poolAddress)
		{
			return S_OK;
		}

		UINT destinationTile = startTile;
		for (UINT i = 0; i < rangeCount; ++i)
		{
			const D3D12XBOX_PAGE_MAPPING_RANGE& range = ranges[i];
			if (range.RangeType == XBOX_RANGE_SKIP_RANGE || !range.PageCount ||
			    destinationTile >= tileCount)
			{
				destinationTile += range.PageCount;
				continue;
			}
			UINT tiles = range.PageCount;
			if (destinationTile + tiles > tileCount)
			{
				tiles = tileCount - destinationTile;
			}

			D3D12_TILED_RESOURCE_COORDINATE coordinate{};
			coordinate.X = destinationTile;
			coordinate.Subresource = 0;
			D3D12_TILE_REGION_SIZE region{};
			region.NumTiles = tiles;
			region.UseBox = FALSE;

			D3D12_TILE_RANGE_FLAGS rangeFlags = D3D12_TILE_RANGE_FLAG_NONE;
			UINT heapStart = range.StartPageIndexInPool;
			UINT count = tiles;
			if (range.RangeType == XBOX_RANGE_NULL_PAGES)
			{
				rangeFlags = D3D12_TILE_RANGE_FLAG_NULL;
				heapStart = 0;
			}
			else if (range.RangeType == XBOX_RANGE_CONSTANT_PAGE_INDEX)
			{
				rangeFlags = D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE;
			}
			mReal->UpdateTileMappings(resource, 1, &coordinate, &region, heap, 1, &rangeFlags,
			                          &heapStart, &count, D3D12_TILE_MAPPING_FLAG_NONE);
			destinationTile += range.PageCount;
		}
		return S_OK;
	}

	// On Xbox the display scan-out is driven by the D3D12.X driver itself, which
	// is why the title imports no DXGI at all. On desktop we own that: create a
	// swapchain against the window the game already made and copy into it.
	bool EnsureSwapChain(ID3D12Resource* source)
	{
		if (GPresenter.swap)
		{
			return true;
		}
		if (!source)
		{
			return false;
		}

		D3D12_RESOURCE_DESC sourceDesc = source->GetDesc();
		HWND window = nullptr;
		EnumWindows(FindGameWindow, (LPARAM)&window);
		if (!window)
		{
			LOGF("PresentX: no top-level window found yet");
			return false;
		}
		{
			// Do NOT call GetWindowText here: it sends WM_GETTEXT synchronously to
			// the window's owning GameThread, and PresentX runs on the RHI thread
			// that the GameThread is routinely blocked waiting on. GetClassName and
			// GetClientRect are served from kernel state and send no messages.
			char className[64] = {};
			GetClassNameA(window, className, sizeof(className));
			RECT client = {};
			GetClientRect(window, &client);
			LOGF("PresentX: chose hwnd=%p class='%s' client=%ldx%ld", window, className,
			     client.right - client.left, client.bottom - client.top);
		}

		if (FAILED(mReal->GetDevice(__uuidof(ID3D12Device), (void**)&GPresenter.device)))
		{
			LOGF("PresentX: could not get device from queue");
			return false;
		}

		// Flip-model needs one of a small set of formats; match the source when we
		// can so CopyResource is legal, otherwise fall back and skip the copy.
		DXGI_FORMAT format = sourceDesc.Format;
		if (format != DXGI_FORMAT_R8G8B8A8_UNORM && format != DXGI_FORMAT_B8G8R8A8_UNORM &&
		    format != DXGI_FORMAT_R10G10B10A2_UNORM && format != DXGI_FORMAT_R16G16B16A16_FLOAT)
		{
			format = DXGI_FORMAT_R8G8B8A8_UNORM;
		}

		IDXGIFactory2* factory = nullptr;
		if (FAILED(CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), (void**)&factory)))
		{
			return false;
		}

		DXGI_SWAP_CHAIN_DESC1 desc = {};
		desc.Width = (UINT)sourceDesc.Width;
		desc.Height = sourceDesc.Height;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = 3;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.Scaling = DXGI_SCALING_STRETCH;

		IDXGISwapChain1* swapChain1 = nullptr;
		HRESULT hr = factory->CreateSwapChainForHwnd(mReal, window, &desc, nullptr, nullptr, &swapChain1);
		factory->Release();
		if (FAILED(hr) || !swapChain1)
		{
			LOGF("PresentX: CreateSwapChainForHwnd hr=0x%08X", (unsigned)hr);
			return false;
		}
		swapChain1->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&GPresenter.swap);
		swapChain1->Release();
		if (!GPresenter.swap)
		{
			return false;
		}

		for (UINT i = 0; i < Presenter::kRing; ++i)
		{
			GPresenter.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
			                                          __uuidof(ID3D12CommandAllocator),
			                                          (void**)&GPresenter.ring[i]);
		}
		GPresenter.allocator = GPresenter.ring[0];
		GPresenter.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
		                               (void**)&GPresenter.fence);
		GPresenter.fenceEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
		GPresenter.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, GPresenter.allocator,
		                                     nullptr, __uuidof(ID3D12GraphicsCommandList),
		                                     (void**)&GPresenter.list);
		if (GPresenter.list)
		{
			GPresenter.list->Close();
		}

		GPresenter.hwnd = window;
		GPresenter.width = (UINT)sourceDesc.Width;
		GPresenter.height = sourceDesc.Height;
		GPresenter.format = format;
		LOGF("PresentX: swapchain created hwnd=%p %ux%u fmt=%u (source fmt=%u)", window,
		     GPresenter.width, GPresenter.height, (unsigned)format, (unsigned)sourceDesc.Format);
		return GPresenter.list != nullptr;
	}

	void PresentFrame(ID3D12Resource* source)
	{
		UINT index = GPresenter.swap->GetCurrentBackBufferIndex();
		ID3D12Resource* back = nullptr;
		if (FAILED(GPresenter.swap->GetBuffer(index, __uuidof(ID3D12Resource), (void**)&back)) || !back)
		{
			return;
		}

		D3D12_RESOURCE_DESC backDesc = back->GetDesc();
		D3D12_RESOURCE_DESC sourceDesc = source ? source->GetDesc() : D3D12_RESOURCE_DESC{};
		bool copyable = source && backDesc.Format == sourceDesc.Format &&
		                backDesc.Width == sourceDesc.Width && backDesc.Height == sourceDesc.Height;

		// Rotate to the next allocator and wait until the GPU has finished the
		// work recorded into it last time.
		GPresenter.current = (GPresenter.current + 1) % Presenter::kRing;
		GPresenter.allocator = GPresenter.ring[GPresenter.current];
		if (GPresenter.fence && GPresenter.ringFence[GPresenter.current] &&
		    GPresenter.fence->GetCompletedValue() < GPresenter.ringFence[GPresenter.current])
		{
			if (GPresenter.fenceEvent &&
			    SUCCEEDED(GPresenter.fence->SetEventOnCompletion(GPresenter.ringFence[GPresenter.current],
			                                                     GPresenter.fenceEvent)))
			{
				WaitForSingleObject(GPresenter.fenceEvent, 1000);
			}
		}
		GPresenter.allocator->Reset();
		GPresenter.list->Reset(GPresenter.allocator, nullptr);

		if (copyable)
		{
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = back;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			GPresenter.list->ResourceBarrier(1, &barrier);
			GPresenter.list->CopyResource(back, source);
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			GPresenter.list->ResourceBarrier(1, &barrier);
		}

		GPresenter.list->Close();
		ID3D12CommandList* lists[] = { GPresenter.list };
		mReal->ExecuteCommandLists(1, lists);
		if (GPresenter.fence)
		{
			GPresenter.ringFence[GPresenter.current] = ++GPresenter.fenceValue;
			mReal->Signal(GPresenter.fence, GPresenter.fenceValue);
		}
		back->Release();

		HRESULT hr = GPresenter.swap->Present(0, 0);
		LONG frame = GDKScarlett::D3D12X::AdvancePresentedFrame();
		if (frame <= 3 || (frame % 120) == 0)
		{
			LOGF("PresentX: frame %ld presented (copy=%s) hr=0x%08X", frame,
			     copyable ? "yes" : "NO - format/size mismatch", (unsigned)hr);
		}
	}

	ID3D12CommandQueue* mReal = nullptr;
	ID3D12Device* mDevice = nullptr;
	volatile LONG mRefs = 1;
};

ID3D12CommandQueue* XboxCommandQueueWrap(ID3D12CommandQueue* real, ID3D12Device* device)
{
	if (!real)
	{
		return nullptr;
	}
	XboxCommandQueue* wrapper = new XboxCommandQueue(real, device);
	LOGF("XboxCommandQueueWrap: real %p -> wrapper %p", real, wrapper);
	return (ID3D12CommandQueue*)wrapper;
}
