#define GDKS_TRACE_TAG "d3d12_x"
#include "Common.h"
#include "XboxDescHeap.h"
#include "XboxDeviceInternal.h"

EXTERN_C const GUID IID_ID3D12Object =
	{ 0xc4fec28f, 0x7966, 0x4e95, { 0x9f, 0x94, 0xf4, 0x31, 0xcb, 0x56, 0xc3, 0xb8 } };
EXTERN_C const GUID IID_ID3D12DeviceChild =
	{ 0x905db94b, 0xa00c, 0x4140, { 0x9d, 0xf5, 0x2b, 0x64, 0xca, 0x9e, 0xa3, 0x57 } };
EXTERN_C const GUID IID_ID3D12Pageable =
	{ 0x63ee58fb, 0x1268, 0x4835, { 0x86, 0xda, 0xf0, 0x08, 0xce, 0x62, 0xf0, 0xd6 } };
EXTERN_C const GUID IID_ID3D12DescriptorHeap =
	{ 0x8efb471d, 0x616c, 0x4f49, { 0x90, 0xf7, 0x12, 0x7b, 0xb7, 0x63, 0xfa, 0x51 } };

struct IXboxDescriptorHeap
{
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) = 0;
	virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
	virtual ULONG STDMETHODCALLTYPE Release() = 0;
	virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* size, void* data) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT size, const void* data) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* data) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetName(LPCWSTR name) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppv) = 0;
	virtual D3D12_DESCRIPTOR_HEAP_DESC* STDMETHODCALLTYPE GetDesc(IXboxDescriptorHeap* heap) = 0;
	virtual UINT64 STDMETHODCALLTYPE GetCpuDescriptorHandleForHeapStart() = 0;
	virtual UINT64 STDMETHODCALLTYPE GetGpuDescriptorHandleForHeapStart() = 0;
};

static const void* GHeapVtable = nullptr;

struct XboxDescriptorHeap final : IXboxDescriptorHeap
{
	ID3D12DescriptorHeap* real;
	ID3D12Device* device;   // for GetDevice
	volatile LONG refs;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
	{
		if (!ppv)
		{
			return E_POINTER;
		}
		if (riid == IID_IUnknown || riid == IID_ID3D12DescriptorHeap ||
		    riid == IID_ID3D12Pageable || riid == IID_ID3D12DeviceChild || riid == IID_ID3D12Object)
		{
			InterlockedIncrement(&refs);
			*ppv = this;
			return S_OK;
		}
		return real->QueryInterface(riid, ppv);
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
			// Drop the heap-resolution registration BEFORE the real release: the
			// runtime reuses freed heap allocations, and a stale registry entry
			// then aliases the live heap and hijacks every bind resolution.
			GDKScarlett::D3D12X::UnregisterCbvHeap(real);
			real->Release();
			delete this;
		}
		return (ULONG)refCount;
	}

	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* size, void* data) override
	{
		return real->GetPrivateData(guid, size, data);
	}

	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT size, const void* data) override
	{
		return real->SetPrivateData(guid, size, data);
	}

	HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* data) override
	{
		return real->SetPrivateDataInterface(guid, data);
	}

	HRESULT STDMETHODCALLTYPE SetName(LPCWSTR name) override
	{
		return real->SetName(name);
	}

	HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppv) override
	{
		// Hand back the device we were created from, so the game keeps seeing the
		// Xbox-shaped device rather than the raw desktop one.
		if (device && ppv)
		{
			return device->QueryInterface(riid, ppv);
		}
		return real->GetDevice(riid, ppv);
	}

	// Xbox passes the return buffer in the this slot and the object second; MSVC does the reverse.
	D3D12_DESCRIPTOR_HEAP_DESC* STDMETHODCALLTYPE GetDesc(IXboxDescriptorHeap* heap) override
	{
		static volatile LONG probed = 0;
		if (InterlockedExchange(&probed, 1) == 0)
			LOGF("heap GetDesc: this=%p param=%p vtable=%p thisIsWrapper=%d", (void*)this,
			     (void*)heap, GHeapVtable, *(const void**)this == GHeapVtable);
		D3D12_DESCRIPTOR_HEAP_DESC* out = (D3D12_DESCRIPTOR_HEAP_DESC*)this;
		*out = static_cast<XboxDescriptorHeap*>(heap)->real->GetDesc();
		return out;
	}

	UINT64 STDMETHODCALLTYPE GetCpuDescriptorHandleForHeapStart() override
	{
		return real->GetCPUDescriptorHandleForHeapStart().ptr;
	}

	UINT64 STDMETHODCALLTYPE GetGpuDescriptorHandleForHeapStart() override
	{
		return real->GetGPUDescriptorHandleForHeapStart().ptr;
	}
};

ID3D12DescriptorHeap* XboxDescriptorHeapWrap(ID3D12DescriptorHeap* real, ID3D12Device* device)
{
	if (!real)
	{
		return nullptr;
	}
	XboxDescriptorHeap* heap = new XboxDescriptorHeap();
	heap->real = real;          // takes ownership of the caller's reference
	heap->device = device;
	heap->refs = 1;
	GHeapVtable = *(const void**)static_cast<IXboxDescriptorHeap*>(heap);
	LOGF("XboxDescriptorHeapWrap: real %p -> wrapper %p", real, heap);
	return (ID3D12DescriptorHeap*)static_cast<IXboxDescriptorHeap*>(heap);
}

// The game hands wrapper pointers straight back to us. The real runtime must never
// see a wrapper, so recognise our own objects by vtable identity and yield the
// real one.
ID3D12DescriptorHeap* XboxDescriptorHeapUnwrap(ID3D12DescriptorHeap* heap)
{
	if (!heap)
	{
		return nullptr;
	}
	if (*(const void**)heap == GHeapVtable)
	{
		return static_cast<XboxDescriptorHeap*>((IXboxDescriptorHeap*)heap)->real;
	}
	return heap;
}
