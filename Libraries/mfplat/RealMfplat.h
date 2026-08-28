#pragma once
#include <windows.h>

namespace GDKScarlett::Mfplat
{
	struct Real
	{
		HRESULT (*mfTEnumEx)(GUID, UINT32, const void*, const void*, void***, UINT32*);
		HRESULT (*mfStartup)(ULONG, DWORD);
		HRESULT (*mfShutdown)();
		HRESULT (*mfCreateMemoryBuffer)(DWORD, void**);
		HRESULT (*mfCreateDXGIDeviceManager)(UINT*, void**);
		HRESULT (*mfCreateAlignedMemoryBuffer)(DWORD, DWORD, void**);
		HRESULT (*mfCreateSample)(void**);
		HRESULT (*mfCreateMediaType)(void**);
		HRESULT (*mfCreateWaveFormatExFromMFMediaType)(void*, WAVEFORMATEX**, UINT32*, UINT32);
		HRESULT (*mfPutWorkItem2)(DWORD, LONG, void*, void*);
		HRESULT (*mfCreateAsyncResult)(void*, void*, void*, void**);
		HRESULT (*mfInvokeCallback)(void*);
		HRESULT (*mfCreateAttributes)(void**, UINT32);
		HRESULT (*mfCreateSourceResolver)(void**);
	};

	const Real& RealMfplat();
}
