#define GDKS_TRACE_TAG "d3d12_x"
#include "Common.h"
#include "XboxDxgiDevice.h"
#include <dxgi.h>
#include <dxgi1_2.h>

#pragma comment(lib, "dxgi.lib")

// dxguid.lib also defines IID_ID3D12Device, which collides with DllMain.cpp's
// override, so we can't link it. Provide the DXGI IIDs we need directly.
EXTERN_C const GUID IID_IDXGIObject =
	{ 0xaec22fb8, 0x76f3, 0x4639, { 0x9b, 0xe0, 0x28, 0xeb, 0x43, 0xa6, 0x7a, 0x2e } };
EXTERN_C const GUID IID_IDXGIDevice =
	{ 0x54ec77fa, 0x1377, 0x44e6, { 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c } };
EXTERN_C const GUID IID_IDXGIDevice1 =
	{ 0x77db970f, 0x6276, 0x48ba, { 0xba, 0x28, 0x07, 0x01, 0x43, 0xb4, 0x39, 0x2c } };
EXTERN_C const GUID IID_IDXGIDevice2 =
	{ 0x05008617, 0xfbfd, 0x4051, { 0xa7, 0x90, 0x14, 0x48, 0x84, 0xb4, 0xf6, 0xa9 } };

struct IXboxDxgiDevice
{
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) = 0;
	virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
	virtual ULONG STDMETHODCALLTYPE Release() = 0;
	virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID name, UINT dataSize, const void* data) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID name, const IUnknown* unknown) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID name, UINT* dataSize, void* data) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetAdapter(IDXGIAdapter** ppAdapter) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateSurface(const DXGI_SURFACE_DESC* desc, UINT surfaceCount, DXGI_USAGE usage,
	                                                const DXGI_SHARED_RESOURCE* sharedResource, IDXGISurface** ppSurface) = 0;
	virtual HRESULT STDMETHODCALLTYPE QueryResourceResidency(IUnknown* const* resources, DXGI_RESIDENCY* residency, UINT resourceCount) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(INT priority) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(INT* priority) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT maxLatency) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* maxLatency) = 0;
	virtual HRESULT STDMETHODCALLTYPE OfferResources(UINT resourceCount, IDXGIResource* const* resources, DXGI_OFFER_RESOURCE_PRIORITY priority) = 0;
	virtual HRESULT STDMETHODCALLTYPE ReclaimResources(UINT resourceCount, IDXGIResource* const* resources, BOOL* discarded) = 0;
	virtual HRESULT STDMETHODCALLTYPE EnqueueSetEvent(HANDLE event) = 0;
};

struct XboxDxgiDevice final : IXboxDxgiDevice
{
	ID3D12Device* device;
	IDXGIAdapter* adapter;
	UINT maxFrameLatency;
	volatile LONG refs;

	// Resolves and caches `adapter` by matching the D3D12 device's LUID against the
	// adapters DXGI enumerates. On success takes a reference the caller must drop.
	HRESULT ResolveAdapter()
	{
		if (adapter)
		{
			adapter->AddRef();
			return S_OK;
		}

		LUID luid = device->GetAdapterLuid();

		IDXGIFactory1* factory = nullptr;
		HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
		if (FAILED(hr))
		{
			LOGF("ResolveAdapter: CreateDXGIFactory1 failed hr=0x%08X", (unsigned)hr);
			return hr;
		}

		IDXGIAdapter1* found = nullptr;
		for (UINT i = 0; ; ++i)
		{
			IDXGIAdapter1* candidate = nullptr;
			if (factory->EnumAdapters1(i, &candidate) != S_OK)
			{
				break;
			}
			DXGI_ADAPTER_DESC1 desc;
			if (SUCCEEDED(candidate->GetDesc1(&desc)) &&
			    desc.AdapterLuid.LowPart == luid.LowPart &&
			    desc.AdapterLuid.HighPart == luid.HighPart)
			{
				found = candidate;
				break;
			}
			candidate->Release();
		}
		factory->Release();

		if (!found)
		{
			LOGF("ResolveAdapter: no adapter matched LUID %08lX:%08lX", luid.HighPart, luid.LowPart);
			return DXGI_ERROR_NOT_FOUND;
		}

		adapter = found;
		adapter->AddRef();
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
	{
		if (!ppvObject)
		{
			return E_POINTER;
		}
		if (riid == IID_IUnknown || riid == IID_IDXGIObject || riid == IID_IDXGIDevice ||
		    riid == IID_IDXGIDevice1 || riid == IID_IDXGIDevice2)
		{
			InterlockedIncrement(&refs);
			*ppvObject = this;
			return S_OK;
		}
		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return InterlockedIncrement(&refs);
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		LONG refCount = InterlockedDecrement(&refs);
		if (refCount == 0)
		{
			if (adapter)
			{
				adapter->Release();
			}
			device->Release();
			delete this;
		}
		return (ULONG)refCount;
	}

	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID name, UINT dataSize, const void* data) override
	{
		return device->SetPrivateData(name, dataSize, data);
	}

	HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID name, const IUnknown* unknown) override
	{
		return device->SetPrivateDataInterface(name, unknown);
	}

	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID name, UINT* dataSize, void* data) override
	{
		return device->GetPrivateData(name, dataSize, data);
	}

	HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent) override
	{
		if (!ppParent)
		{
			return E_POINTER;
		}
		*ppParent = nullptr;
		HRESULT hr = ResolveAdapter();
		if (FAILED(hr))
		{
			return hr;
		}
		hr = adapter->QueryInterface(riid, ppParent);
		adapter->Release();
		return hr;
	}

	HRESULT STDMETHODCALLTYPE GetAdapter(IDXGIAdapter** ppAdapter) override
	{
		if (!ppAdapter)
		{
			return E_POINTER;
		}
		HRESULT hr = ResolveAdapter();
		if (FAILED(hr))
		{
			*ppAdapter = nullptr;
			return hr;
		}
		*ppAdapter = adapter;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE CreateSurface(const DXGI_SURFACE_DESC*, UINT, DXGI_USAGE,
	                                        const DXGI_SHARED_RESOURCE*, IDXGISurface** ppSurface) override
	{
		if (ppSurface)
		{
			*ppSurface = nullptr;
		}
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE QueryResourceResidency(IUnknown* const*, DXGI_RESIDENCY*, UINT) override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(INT) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(INT* priority) override
	{
		if (!priority)
		{
			return E_POINTER;
		}
		*priority = 0;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT maxLatency) override
	{
		maxFrameLatency = maxLatency;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* maxLatency) override
	{
		if (!maxLatency)
		{
			return E_POINTER;
		}
		*maxLatency = maxFrameLatency;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OfferResources(UINT, IDXGIResource* const*, DXGI_OFFER_RESOURCE_PRIORITY) override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE ReclaimResources(UINT, IDXGIResource* const*, BOOL* discarded) override
	{
		if (discarded)
		{
			*discarded = FALSE;
		}
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE EnqueueSetEvent(HANDLE) override
	{
		return E_NOTIMPL;
	}
};

IUnknown* XboxDxgiDeviceCreate(ID3D12Device* realDevice)
{
	XboxDxgiDevice* dxgi = new XboxDxgiDevice();
	dxgi->device = realDevice;
	dxgi->device->AddRef();
	dxgi->adapter = nullptr;
	dxgi->maxFrameLatency = 3;
	dxgi->refs = 1;
	LOGF("XboxDxgiDeviceCreate: real device %p -> dxgi device %p", realDevice, dxgi);
	return (IUnknown*)static_cast<IXboxDxgiDevice*>(dxgi);
}
